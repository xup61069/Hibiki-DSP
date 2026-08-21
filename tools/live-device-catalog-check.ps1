[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $IsWindows) { throw 'Live device catalog probe is Windows-only.' }
$build = Join-Path $repo '.local/live-device-catalog-build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake -S $repo -B $build -DHIBIKI_BUILD_TESTS=ON -DHIBIKI_BUILD_LIVE_PROBES=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $build --config RelWithDebInfo --target hibiki_live_device_catalog_probe --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = Join-Path $build 'tests/RelWithDebInfo/hibiki_live_device_catalog_probe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live device catalog probe failed with exit code $LASTEXITCODE" }
