[CmdletBinding()]
param(
  [switch]$Build,
  [ValidateSet('Auto', 'DesktopCompat', 'WinUICompat', 'FormalWinUI')]
  [string]$Ui = 'Auto',
  [switch]$SmokeTest,
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting,
  [switch]$EnableWasapiOutput,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
$desktop = Join-Path $repo '.local/preview/DesktopCompat/Hibiki.DesktopPreview.exe'
$winui = Join-Path $repo '.local/preview/WinUICompat/Hibiki.WinUI.exe'
$formal = Join-Path $repo '.local/preview/WinUI/Hibiki.WinUI.exe'

function Test-WindowsAppRuntimePackages {
  param([AllowNull()][object[]]$Packages)
  $minimum = [version]'7000.456.1632.0'
  foreach ($package in @($Packages)) {
    if ($null -ne $package -and $package.Architecture -eq 'X64' -and $package.Status -eq 'Ok' -and
        ([version]$package.Version) -ge $minimum) {
      return $true
    }
  }
  return $false
}

function Test-WindowsAppRuntime17X64 {
  try {
    return Test-WindowsAppRuntimePackages @(Get-AppxPackage -Name Microsoft.WindowsAppRuntime.1.7 -ErrorAction Stop)
  } catch {
    return $false
  }
}

function Resolve-SelectedUi {
  param([string]$RequestedUi, [bool]$RuntimePresent)
  $selected = $RequestedUi
  if ($RequestedUi -eq 'Auto') {
    $selected = if ($RuntimePresent) { 'WinUICompat' } else { 'DesktopCompat' }
  }
  # The framework-dependent XAML previews (compatibility and formal) both need
  # the Windows App Runtime; DesktopCompat is self-contained and does not.
  if (($selected -eq 'WinUICompat' -or $selected -eq 'FormalWinUI') -and -not $RuntimePresent) {
    throw "The $selected preview needs Windows App Runtime 1.7 x64 (>= 7000.456.1632.0). Use -Ui DesktopCompat or install the runtime."
  }
  return $selected
}

function Get-EngineArguments {
  param([bool]$SystemVolume, [bool]$SessionRouting, [bool]$WasapiOutput)
  $engineArguments = @()
  if ($SystemVolume) { $engineArguments += '--enable-system-volume' }
  if ($SessionRouting) { $engineArguments += '--enable-session-routing' }
  if ($WasapiOutput) { $engineArguments += '--enable-wasapi-output' }
  return , $engineArguments
}

function Test-PreviewPathUnderRoot {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Candidate
  )

  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate)
  return $resolvedCandidate.StartsWith(
    $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase
  )
}

function Assert-PreviewLaunchTarget {
  param(
    [Parameter(Mandatory = $true)][string]$LocalRoot,
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][System.IO.FileAttributes]$ExecutableAttributes,
    [Parameter(Mandatory = $true)][System.IO.FileAttributes[]]$AncestorAttributes,
    [Parameter(Mandatory = $true)][bool]$IsFile
  )

  if (-not (Test-PreviewPathUnderRoot -Root $LocalRoot -Candidate $ExecutablePath)) {
    throw "Preview launch target is outside the repository-local .local root: $ExecutablePath"
  }
  if (-not $IsFile) {
    throw "Preview launch target must be a file: $ExecutablePath"
  }
  if (($ExecutableAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Preview launch target must not be a reparse point: $ExecutablePath"
  }
  foreach ($attributes in @($AncestorAttributes)) {
    if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Preview launch path contains a reparse-point parent: $ExecutablePath"
    }
  }
}

function Get-PreviewAncestorAttributes {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root
  )

  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $current = [System.IO.Path]::GetFullPath((Split-Path -Parent $Path))
  $attributes = @()
  while ($true) {
    $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
    $attributes += [System.IO.FileAttributes]$item.Attributes
    if ($current -eq $resolvedRoot) {
      return ,$attributes
    }
    $parent = [System.IO.Path]::GetFullPath((Split-Path -Parent $current))
    if ($parent -eq $current -or -not (Test-PreviewPathUnderRoot -Root $resolvedRoot -Candidate $current)) {
      throw "Preview launch path did not resolve to the expected .local root: $Path"
    }
    $current = $parent
  }
}

if ($SelfTest) {
  function Assert-GateRejection {
    param([scriptblock]$Action, [string]$ExpectedPattern, [string]$Label)
    try { & $Action } catch {
      if ("$($_.Exception.Message)" -notmatch $ExpectedPattern) {
        throw ("run-preview self-test case '{0}' failed with an unexpected message: {1}") -f $Label, $_.Exception.Message
      }
      return
    }
    throw ("run-preview self-test case '{0}' expected a rejection matching '{1}' but the launcher passed.") -f $Label, $ExpectedPattern
  }

  function New-RuntimePackage {
    param([string]$Architecture = 'X64', [string]$Status = 'Ok', [string]$Version = '7000.456.1632.0')
    return [pscustomobject]@{ Architecture = $Architecture; Status = $Status; Version = $Version }
  }

  $caseCount = 0

  # Cases 1..3: UI resolution matrix.
  $resolved = Resolve-SelectedUi -RequestedUi 'Auto' -RuntimePresent $true
  if ($resolved -ne 'WinUICompat') { throw "run-preview self-test case 'auto-with-runtime' expected WinUICompat, got $resolved." }
  $caseCount++
  $resolved = Resolve-SelectedUi -RequestedUi 'Auto' -RuntimePresent $false
  if ($resolved -ne 'DesktopCompat') { throw "run-preview self-test case 'auto-without-runtime' expected DesktopCompat, got $resolved." }
  $caseCount++
  foreach ($runtime in @($true, $false)) {
    $resolved = Resolve-SelectedUi -RequestedUi 'DesktopCompat' -RuntimePresent $runtime
    if ($resolved -ne 'DesktopCompat') { throw "run-preview self-test case 'explicit-desktopcompat' expected DesktopCompat, got $resolved." }
    $caseCount++
  }

  # Case 4: explicit WinUICompat with the runtime present passes.
  $resolved = Resolve-SelectedUi -RequestedUi 'WinUICompat' -RuntimePresent $true
  if ($resolved -ne 'WinUICompat') { throw "run-preview self-test case 'explicit-winuicompat-with-runtime' expected WinUICompat, got $resolved." }
  $caseCount++

  # Case 5: explicit WinUICompat without the runtime fails closed with the actionable message.
  Assert-GateRejection -Label 'winuicompat-without-runtime' -ExpectedPattern 'WinUICompat preview needs Windows App Runtime 1\.7 x64 \(>= 7000\.456\.1632\.0\)' `
    -Action { Resolve-SelectedUi -RequestedUi 'WinUICompat' -RuntimePresent $false }
  $caseCount++

  # Case 5b: explicit FormalWinUI with the runtime present passes.
  $resolved = Resolve-SelectedUi -RequestedUi 'FormalWinUI' -RuntimePresent $true
  if ($resolved -ne 'FormalWinUI') { throw "run-preview self-test case 'explicit-formalwinui-with-runtime' expected FormalWinUI, got $resolved." }
  $caseCount++

  # Case 5c: explicit FormalWinUI without the runtime fails closed like WinUICompat.
  Assert-GateRejection -Label 'formalwinui-without-runtime' -ExpectedPattern 'FormalWinUI preview needs Windows App Runtime 1\.7 x64 \(>= 7000\.456\.1632\.0\)' `
    -Action { Resolve-SelectedUi -RequestedUi 'FormalWinUI' -RuntimePresent $false }
  $caseCount++

  # Cases 6..10: runtime package qualification boundary and filtering.
  $qualifying = @('7000.456.1632.0', '7000.500.1000.0', '8000.0.0.0')
  foreach ($version in $qualifying) {
    $present = Test-WindowsAppRuntimePackages @((New-RuntimePackage -Version $version))
    if (-not $present) { throw "run-preview self-test case 'runtime-boundary' expected version $version to qualify." }
    $caseCount++
  }
  foreach ($version in @('7000.456.1631.0', '6999.999.9999.999')) {
    $present = Test-WindowsAppRuntimePackages @((New-RuntimePackage -Version $version))
    if ($present) { throw "run-preview self-test case 'runtime-boundary' expected version $version to be rejected." }
    $caseCount++
  }
  $present = Test-WindowsAppRuntimePackages @((New-RuntimePackage -Architecture 'X86'))
  if ($present) { throw "run-preview self-test case 'architecture-filter' expected an X86 package to be rejected." }
  $caseCount++
  $present = Test-WindowsAppRuntimePackages @((New-RuntimePackage -Status 'NeedsRemediation'))
  if ($present) { throw "run-preview self-test case 'status-filter' expected a non-Ok package to be rejected." }
  $caseCount++
  $present = Test-WindowsAppRuntimePackages @(
    (New-RuntimePackage -Architecture 'X86'),
    (New-RuntimePackage -Version '6000.0.0.0'),
    (New-RuntimePackage -Version '7100.0.0.0'))
  if (-not $present) { throw "run-preview self-test case 'multi-package-fallback' expected the last qualifying package to be found." }
  $caseCount++
  $present = Test-WindowsAppRuntimePackages $null
  if ($present) { throw "run-preview self-test case 'empty-package-list' expected no packages to fail qualification." }
  $caseCount++

  # Case 11: all engine flags off produce no arguments.
  $arguments = Get-EngineArguments -SystemVolume:$false -SessionRouting:$false -WasapiOutput:$false
  if (@($arguments).Count -ne 0) { throw "run-preview self-test case 'engine-args-none' expected zero arguments, got: $($arguments -join ' ')." }
  $caseCount++

  # Cases 12..14: each single flag produces exactly its token.
  $singleCases = @(
    @{ SystemVolume = $true;  SessionRouting = $false; WasapiOutput = $false; Expected = '--enable-system-volume' },
    @{ SystemVolume = $false; SessionRouting = $true;  WasapiOutput = $false; Expected = '--enable-session-routing' },
    @{ SystemVolume = $false; SessionRouting = $false; WasapiOutput = $true;  Expected = '--enable-wasapi-output' }
  )
  foreach ($entry in $singleCases) {
    $arguments = Get-EngineArguments -SystemVolume:$entry.SystemVolume -SessionRouting:$entry.SessionRouting -WasapiOutput:$entry.WasapiOutput
    if (@($arguments).Count -ne 1 -or $arguments[0] -ne $entry.Expected) {
      throw ("run-preview self-test case 'engine-args-single' expected [{0}], got: {1}.") -f $entry.Expected, ($arguments -join ' ')
    }
    $caseCount++
  }

  # Case 15: all flags on produce the exact ordered tokens.
  $arguments = Get-EngineArguments -SystemVolume:$true -SessionRouting:$true -WasapiOutput:$true
  $expectedAll = @('--enable-system-volume', '--enable-session-routing', '--enable-wasapi-output')
  if (($arguments -join ' ') -ne ($expectedAll -join ' ')) {
    throw "run-preview self-test case 'engine-args-all' expected [$($expectedAll -join ' ')], got: $($arguments -join ' ')."
  }
  $caseCount++

  $selfTestLocalRoot = Join-Path $repo '.local'
  $selfTestEngine = Join-Path $selfTestLocalRoot 'engine-preview/Release/hibiki_engine_preview.exe'
  Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestEngine `
    -ExecutableAttributes ([System.IO.FileAttributes]::Normal) `
    -AncestorAttributes @([System.IO.FileAttributes]::Directory, [System.IO.FileAttributes]::Directory, [System.IO.FileAttributes]::Directory) `
    -IsFile $true
  $caseCount++

  $outsideTarget = Join-Path $repo 'outside-preview.exe'
  Assert-GateRejection -Label 'outside-launch-target' -ExpectedPattern 'outside the repository-local \.local root' `
    -Action { Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $outsideTarget `
      -ExecutableAttributes ([System.IO.FileAttributes]::Normal) `
      -AncestorAttributes @([System.IO.FileAttributes]::Directory) -IsFile $true }
  $caseCount++

  Assert-GateRejection -Label 'reparse-launch-target' -ExpectedPattern 'target must not be a reparse point' `
    -Action { Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestEngine `
      -ExecutableAttributes ([System.IO.FileAttributes]::Normal -bor [System.IO.FileAttributes]::ReparsePoint) `
      -AncestorAttributes @([System.IO.FileAttributes]::Directory) -IsFile $true }
  $caseCount++

  Assert-GateRejection -Label 'reparse-launch-parent' -ExpectedPattern 'reparse-point parent' `
    -Action { Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestEngine `
      -ExecutableAttributes ([System.IO.FileAttributes]::Normal) `
      -AncestorAttributes @([System.IO.FileAttributes]::Directory, ([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint)) -IsFile $true }
  $caseCount++

  Assert-GateRejection -Label 'directory-launch-target' -ExpectedPattern 'target must be a file' `
    -Action { Assert-PreviewLaunchTarget -LocalRoot $selfTestLocalRoot -ExecutablePath $selfTestEngine `
      -ExecutableAttributes ([System.IO.FileAttributes]::Directory) `
      -AncestorAttributes @([System.IO.FileAttributes]::Directory) -IsFile $false }
  $caseCount++

  Write-Output "Preview launcher self-test passed ($caseCount cases)."
  exit 0
}

$runtimePresent = Test-WindowsAppRuntime17X64
$selectedUi = Resolve-SelectedUi -RequestedUi $Ui -RuntimePresent $runtimePresent
if ($Ui -eq 'Auto') {
  Write-Output "Auto-selected preview UI: $selectedUi"
}
$uiExecutable = switch ($selectedUi) {
  'WinUICompat' { $winui }
  'FormalWinUI' { $formal }
  default { $desktop }
}
# build-preview.ps1 knows the formal target as 'WinUI'.
$buildTarget = if ($selectedUi -eq 'FormalWinUI') { 'WinUI' } else { $selectedUi }

if ($Build -or -not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
if ($Build -or -not (Test-Path -LiteralPath $uiExecutable)) {
  & (Join-Path $repo 'tools/build-preview.ps1') -Target $buildTarget
  if ($LASTEXITCODE -ne 0) { throw "$selectedUi preview build failed: $LASTEXITCODE" }
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; close it before starting the combined preview.'
}

$engineArguments = @()
if ($EnableSystemVolume) { $engineArguments += '--enable-system-volume' }
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
if ($EnableWasapiOutput) { $engineArguments += '--enable-wasapi-output' }
$localRoot = Join-Path $repo '.local'
$engineItem = Get-Item -LiteralPath $engine -Force -ErrorAction Stop
$engineAncestors = Get-PreviewAncestorAttributes -Path $engine -Root $localRoot
Assert-PreviewLaunchTarget -LocalRoot $localRoot -ExecutablePath $engine `
  -ExecutableAttributes $engineItem.Attributes -AncestorAttributes $engineAncestors -IsFile:$(-not $engineItem.PSIsContainer)
$uiItem = Get-Item -LiteralPath $uiExecutable -Force -ErrorAction Stop
$uiAncestors = Get-PreviewAncestorAttributes -Path $uiExecutable -Root $localRoot
Assert-PreviewLaunchTarget -LocalRoot $localRoot -ExecutablePath $uiExecutable `
  -ExecutableAttributes $uiItem.Attributes -AncestorAttributes $uiAncestors -IsFile:$(-not $uiItem.PSIsContainer)
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments `
  -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
try {
  # Both UI variants can connect once the local control pipe is ready.
  $uiProcess = Start-Process -FilePath $uiExecutable -WorkingDirectory (Split-Path $uiExecutable) -PassThru
  if ($SmokeTest) {
    Start-Sleep -Seconds 3
    $uiProcess.Refresh()
    if ($uiProcess.HasExited) { throw "$selectedUi preview exited during launcher smoke: $($uiProcess.ExitCode)" }
    Stop-Process -Id $uiProcess.Id -ErrorAction SilentlyContinue
    $uiProcess.WaitForExit()
    Write-Output "Hibiki $selectedUi Preview launch smoke passed."
  } else {
    Wait-Process -Id $uiProcess.Id
  }
}
finally {
  if ($null -ne $engineProcess -and -not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
    $engineProcess.WaitForExit()
  }
}

Write-Output "Hibiki $selectedUi Preview closed safely."
