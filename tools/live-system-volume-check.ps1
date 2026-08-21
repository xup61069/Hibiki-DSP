[CmdletBinding()]
param(
  [switch]$WriteTest,
  # Keep a broker-only diagnostic available; the default path exercises the
  # Engine Preview write-through loop.
  [switch]$DirectBroker
)

$ErrorActionPreference = 'Stop'
if (-not $WriteTest) {
  throw 'This opt-in probe writes a temporary -3 dB change and restores it. Re-run with -WriteTest to confirm.'
}

$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo '.local/live-system-volume'
$configureArgs = @(
  '-S', $repo,
  '-B', $build,
  '-DHIBIKI_BUILD_TESTS=ON',
  '-DHIBIKI_BUILD_LIVE_PROBES=ON',
  '-DHIBIKI_BUILD_ENGINE_PREVIEW=OFF'
)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe configure failed: $LASTEXITCODE" }
$target = if ($DirectBroker) { 'hibiki_live_system_volume_probe' } else { 'hibiki_live_engine_system_volume_probe' }
& cmake --build $build --config RelWithDebInfo --target $target -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe build failed: $LASTEXITCODE" }

$probeName = if ($DirectBroker) { 'hibiki_live_system_volume_probe.exe' } else { 'hibiki_live_engine_system_volume_probe.exe' }
$probe = Join-Path $build "tests/RelWithDebInfo/$probeName"
if (-not (Test-Path -LiteralPath $probe)) { throw "Live system-volume probe was not produced: $probe" }
if ($DirectBroker) {
  & $probe
  if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe failed: $LASTEXITCODE" }
  Write-Output 'Live broker-only system-volume probe completed; any temporary attenuation was restored.'
  exit 0
}

$engineBuild = Join-Path $repo 'tools/build-engine-preview.ps1'
& $engineBuild
if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
if (-not (Test-Path -LiteralPath $engine)) { throw "Engine Preview executable missing: $engine" }
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this live probe.'
}
$engineProcess = Start-Process -FilePath $engine -ArgumentList '--enable-system-volume' `
  -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
try {
  & $probe
  if ($LASTEXITCODE -ne 0) { throw "Live Engine system-volume probe failed: $LASTEXITCODE" }
} finally {
  if ($null -ne $engineProcess -and -not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
    $engineProcess.WaitForExit()
  }
}
Write-Output 'Live system-volume probe completed; any temporary attenuation was restored.'
