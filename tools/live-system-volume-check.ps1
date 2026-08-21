[CmdletBinding()]
param(
  [switch]$WriteTest
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
& cmake --build $build --config RelWithDebInfo --target hibiki_live_system_volume_probe -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe build failed: $LASTEXITCODE" }

$probe = Join-Path $build 'tests/RelWithDebInfo/hibiki_live_system_volume_probe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw "Live system-volume probe was not produced: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe failed: $LASTEXITCODE" }
Write-Output 'Live system-volume probe completed; any temporary attenuation was restored.'
