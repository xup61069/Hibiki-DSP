[CmdletBinding()]
param(
  [ValidateSet('WinUI', 'WinUICompat', 'DesktopCompat', 'ControlModel')][string]$Target = 'DesktopCompat',
  [switch]$SmokeTest,
  [switch]$SelfTest
)

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

  Write-Output 'Preview target dispatch self-test passed (7 cases).'

  exit 0
}

$preview = Get-PreviewDispatchMetadata $repo $Target
$previewRoot = $preview.OutputRoot

if ($Target -eq 'ControlModel') {
  $project = $preview.ProjectPath
  dotnet run --project $project --configuration Release --nologo `
    "-p:BaseOutputPath=$previewRoot/bin/" `
    "-p:MSBuildProjectExtensionsPath=$previewRoot/obj/"
  if ($LASTEXITCODE -ne 0) { throw "Control-model preview baseline failed: $LASTEXITCODE" }
  Write-Output "Control-model preview baseline passed. Output stays under $previewRoot and is not publishable."
  return
}

if ($Target -eq 'WinUICompat') {
  # This fallback is intentionally explicit: it is useful to preview the
  # ViewModel on a machine where the App SDK XAML compiler cannot run, but it
  # is not a substitute for the target WinUI/XAML accessibility gate.
  $project = $preview.ProjectPath
  dotnet build $project --configuration Release "-p:OutputPath=$previewRoot/" '-p:HibikiCompatibilityPreview=true' '-p:Platform=x64'
  if ($LASTEXITCODE -ne 0) { throw "Compatibility preview build failed: $LASTEXITCODE" }
  if ($SmokeTest) {
    $executable = Join-Path $previewRoot 'Hibiki.WinUI.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw "Compatibility preview executable was not produced: $executable" }
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

if ($Target -eq 'DesktopCompat') {
  # A self-contained .NET fallback for machines without Windows App Runtime.
  # It shares the control model, but is not the formal WinUI shell.
  $project = $preview.ProjectPath
  dotnet publish $project --configuration Release --runtime win-x64 --self-contained true --output $previewRoot
  if ($LASTEXITCODE -ne 0) { throw "Desktop compatibility preview build failed: $LASTEXITCODE" }
  if ($SmokeTest) {
    $executable = Join-Path $previewRoot 'Hibiki.DesktopPreview.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw "Desktop preview executable was not produced: $executable" }
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
  $executable = Join-Path $previewRoot 'Hibiki.WinUI.exe'
  if (-not (Test-Path -LiteralPath $executable)) { throw "Formal WinUI preview executable was not produced: $executable" }
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
    if ($controls.Count -lt 10) {
      throw "Formal WinUI preview accessibility tree looks empty: controls=$($controls.Count)"
    }
    $summary = [ordered]@{
      schema_version = 1
      window_name    = $windowName
      window_class   = $windowClass
      control_count  = $controls.Count
      captured_at    = (Get-Date).ToUniversalTime().ToString('o')
      limitations    = @('unsigned local preview', 'user-space only', 'not driver, HLK or signing evidence')
    }
    $summaryPath = Join-Path $previewRoot 'winui-launch-a11y-smoke.json'
    $summary | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
    Write-Output "Formal WinUI preview launch/accessibility smoke passed (window='$windowName' class=$windowClass controls=$($controls.Count); summary=$summaryPath)."
  }
  finally {
    if (-not $process.HasExited) {
      Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
      $process.WaitForExit()
    }
  }
}
