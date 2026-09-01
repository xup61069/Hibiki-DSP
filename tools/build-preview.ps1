#Requires -Version 7
[CmdletBinding()]
param(
  [ValidateSet('WinUI', 'WinUICompat', 'DesktopCompat', 'ControlModel')][string]$Target = 'DesktopCompat',
  [switch]$SmokeTest,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-PreviewDispatchMetadata([string]$repoRoot, [string]$target) {
  if ([string]::IsNullOrWhiteSpace($repoRoot)) { throw 'Preview dispatch requires a repository root.' }

  $projectRelativePath = switch ($target) {
    'WinUI' { 'apps/winui-shell/Hibiki.WinUI.csproj' }
    'WinUICompat' { 'apps/winui-shell/Hibiki.WinUI.csproj' }
    'DesktopCompat' { 'apps/desktop-compat-preview/Hibiki.DesktopPreview.csproj' }
    'ControlModel' { 'apps/control-model-check/Hibiki.ControlModel.Check.csproj' }
    default { throw "Unsupported preview target: $target" }
  }

  $buildMode = switch ($target) {
    'WinUI' { 'WinUI' }
    'WinUICompat' { 'WinUICompat' }
    'DesktopCompat' { 'DesktopCompat' }
    'ControlModel' { 'ControlModel' }
  }

  $outputProperty = if ($target -eq 'ControlModel') { 'BaseOutputPath' } else { 'OutputPath' }
  $smokeExecutable = switch ($target) {
    'WinUICompat' { 'Hibiki.WinUI.exe' }
    'DesktopCompat' { 'Hibiki.DesktopPreview.exe' }
    default { $null }
  }

  [pscustomobject]@{
    Target = $target
    BuildMode = $buildMode
    ProjectRelativePath = $projectRelativePath
    ProjectPath = Join-Path $repoRoot $projectRelativePath
    OutputRoot = Join-Path $repoRoot ".local/preview/$target"
    OutputProperty = $outputProperty
    SmokeExecutable = $smokeExecutable
  }
}

function Move-PreviewOutputAside {
  param(
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$RepositoryRoot
  )

  $fullRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd([char]'\', [char]'/')
  $allowedParent = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot '.local/preview')).TrimEnd([char]'\', [char]'/')
  if (-not $fullRoot.StartsWith($allowedParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Clean-output staging refused: output root must stay under <repo>/.local/preview.'
  }
  if (-not (Test-Path -LiteralPath $fullRoot)) { return }
  $leaf = Split-Path -Path $fullRoot -Leaf
  $trashRoot = Join-Path $allowedParent '_trash'
  New-Item -ItemType Directory -Force -Path $trashRoot | Out-Null
  $destination = Join-Path $trashRoot ($leaf + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
  Move-Item -LiteralPath $fullRoot -Destination $destination
  Write-Output ([string]'Staged existing preview output aside: '+$destination)
}

function Find-VisualStudioMsBuild {
  $roots = @()
  foreach ($root in @(${env:ProgramFiles(x86)}, ${env:ProgramFiles})) {
    if (-not [string]::IsNullOrWhiteSpace($root)) {
      $roots += Join-Path $root 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
  }

  $vswhere = $roots | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
  if (-not $vswhere) { return $null }

  $candidate = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null | Select-Object -First 1
  if ($candidate) {
    $resolved = $candidate.ToString().Trim()
    if (Test-Path -LiteralPath $resolved) { return $resolved }
  }
  return $null
}

function Get-FormalWinUiBuildTool([string]$msbuildPath) {
  if ([string]::IsNullOrWhiteSpace($msbuildPath)) { return 'dotnet-fallback' }
  return 'visual-studio-msbuild'
}

function Test-PreviewPathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-PreviewExistingAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Preview path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Preview path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-PreviewBuildOutputRoot {
  param(
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $expectedRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot '.local')).TrimEnd('\', '/')
  $candidate = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
  if (-not (Test-PreviewPathUnderRoot -Path $candidate -Root $expectedRoot)) {
    throw "Preview output root must remain under the repository .local root: $candidate"
  }

  $cursor = $candidate
  while ($true) {
    $attributes = Get-PreviewExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Preview output root or parent is a reparse point: $cursor"
      }
      if (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Preview output root or parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $expectedRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Preview output root could not reach the repository .local root: $candidate"
    }
    $cursor = $parent
  }
}

function Assert-PreviewLaunchTarget {
  param(
    [Parameter(Mandatory)][string]$LocalRoot,
    [Parameter(Mandatory)][string]$ExecutablePath,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $resolvedRoot = [IO.Path]::GetFullPath($LocalRoot).TrimEnd('\', '/')
  $resolvedExecutable = [IO.Path]::GetFullPath($ExecutablePath).TrimEnd('\', '/')
  if (-not (Test-PreviewPathUnderRoot -Path $resolvedExecutable -Root $resolvedRoot)) {
    throw "Preview launch target must remain under the repository .local root: $resolvedExecutable"
  }

  $targetAttributes = Get-PreviewExistingAttributes -Path $resolvedExecutable `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $targetAttributes) {
    throw "Preview launch target does not exist: $resolvedExecutable"
  }
  if (($targetAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Preview launch target must not be a reparse point: $resolvedExecutable"
  }
  if (($targetAttributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
    throw "Preview launch target must be a file: $resolvedExecutable"
  }

  $cursor = [IO.Path]::GetFullPath((Split-Path -Parent $resolvedExecutable)).TrimEnd('\', '/')
  while ($true) {
    $attributes = Get-PreviewExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -eq $attributes) {
      throw "Preview launch target parent does not exist: $cursor"
    }
    if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Preview launch path contains a reparse-point parent: $resolvedExecutable"
    }
    if (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
      throw "Preview launch target parent is not a directory: $cursor"
    }
    if ($cursor -eq $resolvedRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor -or
        -not (Test-PreviewPathUnderRoot -Path $cursor -Root $resolvedRoot)) {
      throw "Preview launch path could not reach the repository .local root: $resolvedExecutable"
    }
    $cursor = $parent
  }
}

function Get-PreviewAutomationCount([object]$AutomationCollection) {
  $countProperty = $AutomationCollection.PSObject.Properties['Count']
  if ($null -ne $countProperty) { return [int]$countProperty.Value }
  return @($AutomationCollection | ForEach-Object { $_ }).Count
}

if ($SelfTest) {
  $expected = [ordered]@{
    WinUI = @{ ProjectRelativePath = 'apps/winui-shell/Hibiki.WinUI.csproj'; OutputProperty = 'OutputPath'; SmokeExecutable = $null }
    WinUICompat = @{ ProjectRelativePath = 'apps/winui-shell/Hibiki.WinUI.csproj'; OutputProperty = 'OutputPath'; SmokeExecutable = 'Hibiki.WinUI.exe' }
    DesktopCompat = @{ ProjectRelativePath = 'apps/desktop-compat-preview/Hibiki.DesktopPreview.csproj'; OutputProperty = 'OutputPath'; SmokeExecutable = 'Hibiki.DesktopPreview.exe' }
    ControlModel = @{ ProjectRelativePath = 'apps/control-model-check/Hibiki.ControlModel.Check.csproj'; OutputProperty = 'BaseOutputPath'; SmokeExecutable = $null }
  }
  $outputRoots = @{}
  foreach ($case in $expected.GetEnumerator()) {
    $metadata = Get-PreviewDispatchMetadata $repo $case.Key
    $expectedOutputRoot = [IO.Path]::GetFullPath((Join-Path $repo ".local/preview/$($case.Key)"))
    if ($metadata.ProjectRelativePath -ne $case.Value.ProjectRelativePath -or
        $metadata.OutputProperty -ne $case.Value.OutputProperty -or
        $metadata.SmokeExecutable -ne $case.Value.SmokeExecutable -or
        [IO.Path]::GetFullPath($metadata.OutputRoot) -ne $expectedOutputRoot) {
      throw "Preview dispatch self-test mapping mismatch for target $($case.Key)."
    }
    Assert-PreviewBuildOutputRoot -OutputRoot $metadata.OutputRoot -RepositoryRoot $repo
    $outputRoots[[IO.Path]::GetFullPath($metadata.OutputRoot)] = $case.Key
  }
  if ($outputRoots.Count -ne $expected.Count) {
    throw 'Preview dispatch self-test found colliding target output roots.'
  }

  $invalidCaught = $false
  try {
    [void](Get-PreviewDispatchMetadata $repo 'Unsupported')
  } catch {
    $invalidCaught = $_.Exception.Message -match 'Unsupported preview target'
  }
    if (-not $invalidCaught) { throw 'Preview dispatch self-test expected unsupported-target rejection.' }

  if ((Get-FormalWinUiBuildTool $null) -ne 'dotnet-fallback') {
    throw 'Preview dispatch self-test expected the null MSBuild path to use the explicit dotnet fallback.'
  }
  if ((Get-FormalWinUiBuildTool 'synthetic/MSBuild.exe') -ne 'visual-studio-msbuild') {
    throw 'Preview dispatch self-test expected a resolved MSBuild path to use Visual Studio MSBuild.'
  }

  $outsideRootCaught = $false
  try {
    Assert-PreviewBuildOutputRoot -OutputRoot (Join-Path $repo 'build') -RepositoryRoot $repo
  } catch {
    $outsideRootCaught = $_.Exception.Message -match 'under the repository .local root'
  }
  if (-not $outsideRootCaught) { throw 'Preview dispatch self-test expected an outside-root rejection.' }

  $localRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local')).TrimEnd('\', '/')
  $syntheticChild = [IO.Path]::GetFullPath((Join-Path $repo '.local/preview/Synthetic')).TrimEnd('\', '/')
  $reparseParentCaught = $false
  try {
    Assert-PreviewBuildOutputRoot -OutputRoot $syntheticChild -RepositoryRoot $repo `
      -SyntheticAttributes @{ $localRoot = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint }
  } catch {
    $reparseParentCaught = $_.Exception.Message -match 'reparse point'
  }
  if (-not $reparseParentCaught) { throw 'Preview dispatch self-test expected a reparse-parent rejection.' }

  $reparseTargetCaught = $false
  try {
    Assert-PreviewBuildOutputRoot -OutputRoot $syntheticChild -RepositoryRoot $repo `
      -SyntheticAttributes @{ $syntheticChild = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint }
  } catch {
    $reparseTargetCaught = $_.Exception.Message -match 'reparse point'
  }
  if (-not $reparseTargetCaught) { throw 'Preview dispatch self-test expected a reparse-target rejection.' }

  $nonDirectoryCaught = $false
  try {
    Assert-PreviewBuildOutputRoot -OutputRoot $syntheticChild -RepositoryRoot $repo `
      -SyntheticAttributes @{ $syntheticChild = [System.IO.FileAttributes]::Archive }
  } catch {
    $nonDirectoryCaught = $_.Exception.Message -match 'not a directory'
  }
  if (-not $nonDirectoryCaught) { throw 'Preview dispatch self-test expected a non-directory rejection.' }

  $selfTestLocalRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local')).TrimEnd('\', '/')
  $selfTestOutputRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local/preview/Synthetic')).TrimEnd('\', '/')
  $selfTestOutputParent = [IO.Path]::GetFullPath((Split-Path -Parent $selfTestOutputRoot)).TrimEnd('\', '/')
  $selfTestExecutable = [IO.Path]::GetFullPath((Join-Path $selfTestOutputRoot 'Hibiki.WinUI.exe')).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive

  Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable -SyntheticAttributes @{
    $selfTestLocalRoot = $directory
    $selfTestOutputParent = $directory
    $selfTestOutputRoot = $directory
    $selfTestExecutable = $file
  }

  $outsideLaunchCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath (Join-Path $repo 'outside-preview.exe') `
      -SyntheticAttributes @{ $selfTestLocalRoot = $directory }
  } catch { $outsideLaunchCaught = $_.Exception.Message -match 'under the repository \.local root' }
  if (-not $outsideLaunchCaught) { throw 'Preview dispatch self-test expected an outside launch-target rejection.' }

  $reparseLaunchTargetCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable -SyntheticAttributes @{
      $selfTestLocalRoot = $directory
      $selfTestOutputParent = $directory
      $selfTestOutputRoot = $directory
      $selfTestExecutable = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseLaunchTargetCaught = $_.Exception.Message -match 'target must not be a reparse point' }
  if (-not $reparseLaunchTargetCaught) { throw 'Preview dispatch self-test expected a reparse launch-target rejection.' }

  $reparseLaunchParentCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable -SyntheticAttributes @{
      $selfTestLocalRoot = $directory
      $selfTestOutputParent = $directory
      $selfTestOutputRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $selfTestExecutable = $file
    }
  } catch { $reparseLaunchParentCaught = $_.Exception.Message -match 'reparse-point parent' }
  if (-not $reparseLaunchParentCaught) { throw 'Preview dispatch self-test expected a reparse launch-parent rejection.' }

  $directoryLaunchTargetCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable -SyntheticAttributes @{
      $selfTestLocalRoot = $directory
      $selfTestOutputParent = $directory
      $selfTestOutputRoot = $directory
      $selfTestExecutable = $directory
    }
  } catch { $directoryLaunchTargetCaught = $_.Exception.Message -match 'target must be a file' }
  if (-not $directoryLaunchTargetCaught) { throw 'Preview dispatch self-test expected a directory launch-target rejection.' }

  $missingLaunchTargetCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable -SyntheticAttributes @{
      $selfTestLocalRoot = $directory
      $selfTestOutputParent = $directory
      $selfTestOutputRoot = $directory
    }
  } catch { $missingLaunchTargetCaught = $_.Exception.Message -match 'target does not exist' }
  if (-not $missingLaunchTargetCaught) { throw 'Preview dispatch self-test expected a missing launch-target rejection.' }

  $leafInspectionErrorCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable `
      -SyntheticAttributes @{ $selfTestOutputParent = $directory; $selfTestOutputRoot = $directory } `
      -SyntheticInspectionErrors @{ $selfTestExecutable = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'Preview dispatch self-test expected a leaf inspection-error rejection.' }

  $parentInspectionErrorCaught = $false
  try {
    Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestExecutable `
      -SyntheticAttributes @{ $selfTestOutputParent = $directory; $selfTestOutputRoot = $directory; $selfTestExecutable = $file } `
      -SyntheticInspectionErrors @{ $selfTestLocalRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'Preview dispatch self-test expected a parent inspection-error rejection.' }

  $stagingRepo = Join-Path ([IO.Path]::GetTempPath()) ('build-preview-staging-' + [guid]::NewGuid().ToString('N'))
  try {
    $previewDir = Join-Path $stagingRepo '.local/preview'
    $staleRoot = Join-Path $previewDir 'WinUICompat'
    New-Item -ItemType Directory -Force -Path (Join-Path $staleRoot 'bin') | Out-Null
    Set-Content -LiteralPath (Join-Path $staleRoot 'bin/stale.txt') -Value 'stale'
    Move-PreviewOutputAside -OutputRoot $staleRoot -RepositoryRoot $stagingRepo
    $movedItems = @(Get-ChildItem -LiteralPath (Join-Path $previewDir '_trash') -Filter 'WinUICompat-*' -Directory)
    if ((Test-Path -LiteralPath $staleRoot) -or ($movedItems.Count -ne 1) -or -not (Test-Path (Join-Path $movedItems[0].FullName 'bin/stale.txt'))) { throw 'Clean-output staging failed to move the stale output directory.' }
    Move-PreviewOutputAside -OutputRoot (Join-Path $previewDir 'DesktopCompat') -RepositoryRoot $stagingRepo
    $outsideCaught = $false
    try { Move-PreviewOutputAside -OutputRoot (Join-Path $stagingRepo 'outside/WinUICompat') -RepositoryRoot $stagingRepo } catch { $outsideCaught = $_.Exception.Message -match 'output root must stay under' }
    if (-not $outsideCaught) { throw 'Expected outside-root rejection from clean-output staging.' }
  } finally {
    Remove-Item -LiteralPath $stagingRepo -Recurse -Force -ErrorAction SilentlyContinue
  }
  Write-Output 'Preview target dispatch self-test passed (22 cases).'


  exit 0
}

$preview = Get-PreviewDispatchMetadata $repo $Target
$previewRoot = $preview.OutputRoot
Assert-PreviewBuildOutputRoot -OutputRoot $previewRoot -RepositoryRoot $repo

if ($Target -eq 'ControlModel') {
  $project = $preview.ProjectPath
  dotnet run --project $project --configuration Release --nologo `
    "-p:BaseOutputPath=$previewRoot/bin/" `
    "-p:MSBuildProjectExtensionsPath=$previewRoot/obj/"
  if ($LASTEXITCODE -ne 0) { throw "Control-model preview baseline failed: $LASTEXITCODE" }
  Write-Output "Control-model preview baseline passed. Output stays under $previewRoot and is not publishable."
  return
}

  Move-PreviewOutputAside -OutputRoot $previewRoot -RepositoryRoot $repo
if ($Target -eq 'WinUICompat') {
  # This fallback is intentionally explicit: it is useful to preview the
  # ViewModel on a machine where the App SDK XAML compiler cannot run, but it
  # is not a substitute for the target WinUI/XAML accessibility gate.
  $project = $preview.ProjectPath
  dotnet build $project --configuration Release "-p:OutputPath=$previewRoot/" '-p:HibikiCompatibilityPreview=true' '-p:Platform=x64'
  if ($LASTEXITCODE -ne 0) { throw "Compatibility preview build failed: $LASTEXITCODE" }
  if ($SmokeTest) {
    Assert-PreviewBuildOutputRoot -OutputRoot $previewRoot -RepositoryRoot $repo
    $executable = Join-Path $previewRoot 'Hibiki.WinUI.exe'
    Assert-PreviewLaunchTarget -LocalRoot (Join-Path $repo '.local') -ExecutablePath $executable
    $process = Start-Process -FilePath $executable -WorkingDirectory $previewRoot -PassThru
    Start-Sleep -Seconds 3
    $process.Refresh()
    if ($process.HasExited) { throw "Compatibility WinUI preview exited during smoke test: $($process.ExitCode)" }
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    $process.WaitForExit()
    Write-Output "Compatibility WinUI preview launch smoke passed."
  }
  Write-Output "Compatibility WinUI preview build succeeded. It is unsigned, driver-free, not release evidence and excluded from Git."
  return
}

  Move-PreviewOutputAside -OutputRoot $previewRoot -RepositoryRoot $repo
if ($Target -eq 'DesktopCompat') {
  # A self-contained .NET fallback for machines without Windows App Runtime.
  # It shares the control model, but is not the formal WinUI shell.
  $project = $preview.ProjectPath
  dotnet publish $project --configuration Release --runtime win-x64 --self-contained true --output $previewRoot
  if ($LASTEXITCODE -ne 0) { throw "Desktop compatibility preview build failed: $LASTEXITCODE" }
  if ($SmokeTest) {
    Assert-PreviewBuildOutputRoot -OutputRoot $previewRoot -RepositoryRoot $repo
    $executable = Join-Path $previewRoot 'Hibiki.DesktopPreview.exe'
    Assert-PreviewLaunchTarget -LocalRoot (Join-Path $repo '.local') -ExecutablePath $executable
    $process = Start-Process -FilePath $executable -WorkingDirectory $previewRoot -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    if ($process.HasExited) { throw "Desktop compatibility preview exited during smoke test: $($process.ExitCode)" }
    Stop-Process -Id $process.Id
    $process.WaitForExit()
    Write-Output "Desktop compatibility preview launch smoke passed."
  }
  Write-Output "Self-contained desktop preview build succeeded. It needs no Windows App Runtime, is driver-free and excluded from Git."
  return
}

# This is deliberately a local developer preview only. It does not build a
# virtual driver, sign anything, stage an installer, or create a GitHub asset.
$project = $preview.ProjectPath
$msbuild = Find-VisualStudioMsBuild
$buildTool = Get-FormalWinUiBuildTool $msbuild
if ($msbuild) {
  Write-Output 'Using Visual Studio MSBuild for the formal WinUI target.'
  & $msbuild $project '/restore' '/nologo' '/t:Build' '/p:Configuration=Release' '/p:Platform=x64' "/p:OutputPath=$previewRoot\"
} else {
  Write-Warning 'Visual Studio MSBuild was not found; using an explicit dotnet fallback. This cannot be promoted to formal XAML/accessibility evidence on a target machine.'
  dotnet build $project --configuration Release "-p:OutputPath=$previewRoot/"
}
if ($LASTEXITCODE -ne 0) {
  throw "WinUI preview build failed via $buildTool for project $($preview.ProjectRelativePath). Check tools/doctor.ps1 and the locked Windows 11 24H2, VS 2026 and SDK/WDK toolchain before treating any output as evidence."
}
Write-Output "WinUI local preview build succeeded via $buildTool. It remains unsigned, driver-free and excluded from Git."

if ($SmokeTest) {
  # Formal WinUI launch + accessibility smoke: the built XAML shell must come
  # up, expose a named top-level window and a populated UI Automation tree.
  # The summary stays under .local; nothing here is release evidence.
  if ($buildTool -eq 'dotnet-fallback') {
    throw 'WinUI smoke test requires the Visual Studio MSBuild formal build; the dotnet fallback cannot be promoted to launch/accessibility evidence.'
  }
  Add-Type -AssemblyName UIAutomationClient
  Add-Type -AssemblyName UIAutomationTypes
  Assert-PreviewBuildOutputRoot -OutputRoot $previewRoot -RepositoryRoot $repo
  $executable = Join-Path $previewRoot 'Hibiki.WinUI.exe'
  Assert-PreviewLaunchTarget -LocalRoot (Join-Path $repo '.local') -ExecutablePath $executable
  $process = Start-Process -FilePath $executable -WorkingDirectory $previewRoot -PassThru
  try {
    $rootElement = $null
    for ($attempt = 0; $attempt -lt 10 -and -not $rootElement; $attempt++) {
      Start-Sleep -Seconds 1
      $process.Refresh()
      if ($process.HasExited) { throw "Formal WinUI preview exited during smoke test: $($process.ExitCode)" }
      if ($process.MainWindowHandle -eq [IntPtr]::Zero) { continue }
      try { $rootElement = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle) } catch { $rootElement = $null }
    }
    if (-not $rootElement) { throw 'Formal WinUI preview did not expose an automation window within the bounded wait.' }
    $windowName = $rootElement.Current.Name
    $windowClass = $rootElement.Current.ClassName
    if ([string]::IsNullOrWhiteSpace($windowName)) { throw 'Formal WinUI preview window has no automation Name.' }
    $controls = $rootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                                     [System.Windows.Automation.Condition]::TrueCondition)
    $controlCount = Get-PreviewAutomationCount $controls
    if ($controlCount -lt 10) {
      throw "Formal WinUI preview accessibility tree looks empty: controls=$controlCount"
    }
    $summary = [ordered]@{
      schema_version = 1
      window_name    = $windowName
      window_class   = $windowClass
      control_count  = $controlCount
      captured_at    = (Get-Date).ToUniversalTime().ToString('o')
      limitations    = @('unsigned local preview', 'user-space only', 'not driver or physical-audio evidence')
    }
    $summaryPath = Join-Path $previewRoot 'winui-launch-a11y-smoke.json'
    $summary | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
    Write-Output "Formal WinUI preview launch/accessibility smoke passed (window='$windowName' class=$windowClass controls=$controlCount; summary=$summaryPath)."
  }
  finally {
    if (-not $process.HasExited) {
      Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
      $process.WaitForExit()
    }
  }
}
