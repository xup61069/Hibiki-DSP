[CmdletBinding()]
param(
    # This probe attenuates only a temporary silent session created by the
    # probe process, reads it back, and restores it. Make the write explicit.
    [switch]$WriteTest,
    # Keep a direct coordinator-only diagnostic available when debugging the
    # COM adapter. The default path exercises the Engine Preview IPC queue.
    [switch]$DirectCoordinator
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
$target = if ($DirectCoordinator) { 'hibiki_live_session_volume_probe' } else { 'hibiki_live_engine_session_volume_probe' }
cmake --build $build --config RelWithDebInfo --target $target -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probeName = if ($DirectCoordinator) { 'hibiki_live_session_volume_probe.exe' } else { 'hibiki_live_engine_session_volume_probe.exe' }
$probe = Join-Path $build "tests/RelWithDebInfo/$probeName"
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
if ($DirectCoordinator) {
    & $probe
    if ($LASTEXITCODE -ne 0) { throw "Live session-volume probe failed with exit code $LASTEXITCODE" }
    exit 0
}

$engineBuild = Join-Path $repo 'tools/build-engine-preview.ps1'
& $engineBuild
if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed with exit code $LASTEXITCODE" }
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
if (-not (Test-Path -LiteralPath $engine)) { throw "Engine Preview executable missing: $engine" }
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Another Engine Preview process is already running; stop it before running this live probe.'
}
$engineProcess = Start-Process -FilePath $engine -ArgumentList '--enable-session-routing' `
    -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
try {
    & $probe
    if ($LASTEXITCODE -ne 0) { throw "Live Engine session-volume probe failed with exit code $LASTEXITCODE" }
} finally {
    if ($null -ne $engineProcess -and -not $engineProcess.HasExited) {
        Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
        $engineProcess.WaitForExit()
    }
}
