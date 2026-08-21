[CmdletBinding()]
param(
  [switch]$Build,
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
$desktop = Join-Path $repo '.local/preview/DesktopCompat/Hibiki.DesktopPreview.exe'

if ($Build -or -not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
if ($Build -or -not (Test-Path -LiteralPath $desktop)) {
  & (Join-Path $repo 'tools/build-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Desktop Compatibility Preview build failed: $LASTEXITCODE" }
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; close it before starting the combined preview.'
}

$engineArguments = @()
if ($EnableSystemVolume) { $engineArguments += '--enable-system-volume' }
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments `
  -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
try {
  # The desktop preview auto-connects once the local control pipe is ready.
  $desktopProcess = Start-Process -FilePath $desktop -WorkingDirectory (Split-Path $desktop) -PassThru
  Wait-Process -Id $desktopProcess.Id
}
finally {
  if ($null -ne $engineProcess -and -not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
    $engineProcess.WaitForExit()
  }
}

Write-Output 'Hibiki Desktop Compatibility Preview closed safely.'
