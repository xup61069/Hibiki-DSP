[CmdletBinding()]
param(
  [switch]$Build,
  [ValidateSet('Auto', 'DesktopCompat', 'WinUICompat')]
  [string]$Ui = 'Auto',
  [switch]$SmokeTest,
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting,
  [switch]$EnableWasapiOutput
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
$desktop = Join-Path $repo '.local/preview/DesktopCompat/Hibiki.DesktopPreview.exe'
$winui = Join-Path $repo '.local/preview/WinUICompat/Hibiki.WinUI.exe'

function Test-WindowsAppRuntime17X64 {
  try {
    $minimum = [version]'7000.456.1632.0'
    foreach ($package in @(Get-AppxPackage -Name Microsoft.WindowsAppRuntime.1.7 -ErrorAction Stop)) {
      if ($package.Architecture -eq 'X64' -and $package.Status -eq 'Ok' -and
          ([version]$package.Version) -ge $minimum) {
        return $true
      }
    }
  } catch {
    return $false
  }
  return $false
}

$selectedUi = $Ui
if ($Ui -eq 'Auto') {
  $selectedUi = if (Test-WindowsAppRuntime17X64) { 'WinUICompat' } else { 'DesktopCompat' }
  Write-Output "Auto-selected preview UI: $selectedUi"
}
if ($selectedUi -eq 'WinUICompat' -and -not (Test-WindowsAppRuntime17X64)) {
  throw 'WinUICompat needs Windows App Runtime 1.7 x64 (>= 7000.456.1632.0). Use -Ui DesktopCompat or install the runtime.'
}
$uiExecutable = if ($selectedUi -eq 'WinUICompat') { $winui } else { $desktop }

if ($Build -or -not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
if ($Build -or -not (Test-Path -LiteralPath $uiExecutable)) {
  & (Join-Path $repo 'tools/build-preview.ps1') -Target $selectedUi
  if ($LASTEXITCODE -ne 0) { throw "$selectedUi preview build failed: $LASTEXITCODE" }
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; close it before starting the combined preview.'
}

$engineArguments = @()
if ($EnableSystemVolume) { $engineArguments += '--enable-system-volume' }
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
if ($EnableWasapiOutput) { $engineArguments += '--enable-wasapi-output' }
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
