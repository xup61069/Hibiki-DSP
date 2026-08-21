[CmdletBinding()]
param(
    # This probe attenuates only a temporary silent session created by the
    # probe process, reads it back, and restores it. Make the write explicit.
    [switch]$WriteTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $IsWindows) { throw 'Live Windows session-volume probe is Windows-only.' }
if (-not $WriteTest) {
    throw 'No session-volume write was performed. Re-run with -WriteTest to create a silent test session, attenuate it, read it back, and restore it.'
}

$build = Join-Path $repo '.local/live-session-volume-build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake -S $repo -B $build -DHIBIKI_BUILD_TESTS=ON -DHIBIKI_BUILD_LIVE_PROBES=ON -DHIBIKI_BUILD_ENGINE_PREVIEW=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $build --config RelWithDebInfo --target hibiki_live_session_volume_probe -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = Join-Path $build 'tests/RelWithDebInfo/hibiki_live_session_volume_probe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live session-volume probe failed with exit code $LASTEXITCODE" }
