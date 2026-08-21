[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $IsWindows) { throw 'Live Windows audio-session probe is Windows-only.' }
$build = Join-Path $repo '.local/live-audio-session-build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake -S $repo -B $build -DHIBIKI_BUILD_TESTS=ON -DHIBIKI_BUILD_LIVE_PROBES=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $build --config RelWithDebInfo --target hibiki_live_audio_session_probe --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = Join-Path $build 'tests/RelWithDebInfo/hibiki_live_audio_session_probe.exe'
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live audio-session probe failed with exit code $LASTEXITCODE" }
