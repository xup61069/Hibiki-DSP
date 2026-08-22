[CmdletBinding()]
param(
    # This probe attenuates only a temporary silent session created by the
    # probe process, reads it back, and restores it. Make the write explicit.
    [switch]$WriteTest,
    # Keep a direct coordinator-only diagnostic available when debugging the
    # COM adapter. The default path exercises the Engine Preview IPC queue.
    [switch]$DirectCoordinator,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
function Assert-LiveSessionVolumeOptIn([bool]$windowsHost, [bool]$writeTest, [bool]$selfTest) {
    if (-not $windowsHost) { throw 'Live Windows session-volume probe is Windows-only.' }
    if (-not $writeTest -and -not $selfTest) {
        throw 'No session-volume write was performed. Re-run with -WriteTest to create a silent test session, attenuate it, read it back, and restore it.'
    }
}

function Get-LiveSessionVolumePlan([string]$repoRoot, [bool]$directCoordinator) {
    if ([string]::IsNullOrWhiteSpace($repoRoot)) { throw 'Live session-volume plan requires a repository root.' }

    $buildRoot = Join-Path $repoRoot '.local/live-session-volume-build'
    $configureArgs = @(
        '-S', $repoRoot,
        '-B', $buildRoot,
        '-DHIBIKI_BUILD_TESTS=ON',
        '-DHIBIKI_BUILD_LIVE_PROBES=ON',
        '-DHIBIKI_BUILD_ENGINE_PREVIEW=OFF'
    )
    $target = if ($directCoordinator) { 'hibiki_live_session_volume_probe' } else { 'hibiki_live_engine_session_volume_probe' }
    $probeName = if ($directCoordinator) { 'hibiki_live_session_volume_probe.exe' } else { 'hibiki_live_engine_session_volume_probe.exe' }
    $engineArguments = if ($directCoordinator) { @() } else { @('--enable-session-routing') }

    [pscustomobject]@{
        DirectCoordinator = $directCoordinator
        BuildRoot = $buildRoot
        ConfigureArgs = $configureArgs
        Target = $target
        ProbeName = $probeName
        ProbePath = Join-Path $buildRoot "tests/RelWithDebInfo/$probeName"
        EngineBuildScript = if ($directCoordinator) { $null } else { Join-Path $repoRoot 'tools/build-engine-preview.ps1' }
        EnginePath = if ($directCoordinator) { $null } else { Join-Path $repoRoot '.local/engine-preview/Release/hibiki_engine_preview.exe' }
        EngineArguments = $engineArguments
    }
}

function Assert-LiveSessionVolumePlan([pscustomobject]$plan, [string]$repoRoot, [bool]$directCoordinator) {
    $expectedBuildRoot = Join-Path $repoRoot '.local/live-session-volume-build'
    $expectedConfigureArgs = @(
        '-S', $repoRoot,
        '-B', $expectedBuildRoot,
        '-DHIBIKI_BUILD_TESTS=ON',
        '-DHIBIKI_BUILD_LIVE_PROBES=ON',
        '-DHIBIKI_BUILD_ENGINE_PREVIEW=OFF'
    )
    $expectedTarget = if ($directCoordinator) { 'hibiki_live_session_volume_probe' } else { 'hibiki_live_engine_session_volume_probe' }
    $expectedProbeName = if ($directCoordinator) { 'hibiki_live_session_volume_probe.exe' } else { 'hibiki_live_engine_session_volume_probe.exe' }
    $expectedProbePath = Join-Path $expectedBuildRoot "tests/RelWithDebInfo/$expectedProbeName"
    $expectedEngineBuildScript = if ($directCoordinator) { $null } else { Join-Path $repoRoot 'tools/build-engine-preview.ps1' }
    $expectedEnginePath = if ($directCoordinator) { $null } else { Join-Path $repoRoot '.local/engine-preview/Release/hibiki_engine_preview.exe' }
    $expectedEngineArguments = if ($directCoordinator) { @() } else { @('--enable-session-routing') }

    if ([IO.Path]::GetFullPath($plan.BuildRoot) -ne [IO.Path]::GetFullPath($expectedBuildRoot) -or
        (@($plan.ConfigureArgs) -join "`n") -ne ($expectedConfigureArgs -join "`n") -or
        $plan.Target -ne $expectedTarget -or
        $plan.ProbeName -ne $expectedProbeName -or
        [IO.Path]::GetFullPath($plan.ProbePath) -ne [IO.Path]::GetFullPath($expectedProbePath) -or
        $plan.EngineBuildScript -ne $expectedEngineBuildScript -or
        $plan.EnginePath -ne $expectedEnginePath -or
        (@($plan.EngineArguments) -join "`n") -ne ($expectedEngineArguments -join "`n")) {
        throw "Live session-volume plan mismatch for directCoordinator=$directCoordinator."
    }
}

$repo = Split-Path -Parent $PSScriptRoot
if ($SelfTest) {
    $windowsCaught = $false
    try {
        Assert-LiveSessionVolumeOptIn $false $true $true
    } catch {
        $windowsCaught = $_.Exception.Message -match 'Windows-only'
    }
    if (-not $windowsCaught) { throw 'Live session-volume self-test expected Windows-only rejection.' }

    $writeTestCaught = $false
    try {
        Assert-LiveSessionVolumeOptIn $true $false $false
    } catch {
        $writeTestCaught = $_.Exception.Message -match 'Re-run with -WriteTest'
    }
    if (-not $writeTestCaught) { throw 'Live session-volume self-test expected WriteTest opt-in rejection.' }
    Assert-LiveSessionVolumeOptIn $true $false $true

    $directPlan = Get-LiveSessionVolumePlan $repo $true
    Assert-LiveSessionVolumePlan $directPlan $repo $true
    $enginePlan = Get-LiveSessionVolumePlan $repo $false
    Assert-LiveSessionVolumePlan $enginePlan $repo $false

    $mismatchCaught = $false
    try {
        Assert-LiveSessionVolumePlan $directPlan $repo $false
    } catch {
        $mismatchCaught = $_.Exception.Message -match 'plan mismatch'
    }
    if (-not $mismatchCaught) { throw 'Live session-volume self-test expected direct/engine plan mismatch rejection.' }

    $unsafePlan = Get-LiveSessionVolumePlan $repo $true
    $unsafePlan.BuildRoot = Join-Path $repo 'outside-live-session-volume'
    $unsafeCaught = $false
    try {
        Assert-LiveSessionVolumePlan $unsafePlan $repo $true
    } catch {
        $unsafeCaught = $_.Exception.Message -match 'plan mismatch'
    }
    if (-not $unsafeCaught) { throw 'Live session-volume self-test expected unsafe build-root rejection.' }

    Write-Output 'Live session-volume wrapper self-test passed (7 cases).'
    exit 0
}

Assert-LiveSessionVolumeOptIn $IsWindows $WriteTest.IsPresent $false

$plan = Get-LiveSessionVolumePlan $repo $DirectCoordinator.IsPresent
$build = $plan.BuildRoot
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
$target = $plan.Target
cmake --build $build --config RelWithDebInfo --target $target -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = $plan.ProbePath
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
if ($DirectCoordinator) {
    & $probe
    if ($LASTEXITCODE -ne 0) { throw "Live session-volume probe failed with exit code $LASTEXITCODE" }
    exit 0
}

$engineBuild = $plan.EngineBuildScript
& $engineBuild
if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed with exit code $LASTEXITCODE" }
$engine = $plan.EnginePath
if (-not (Test-Path -LiteralPath $engine)) { throw "Engine Preview executable missing: $engine" }
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'Another Engine Preview process is already running; stop it before running this live probe.'
}
$engineProcess = Start-Process -FilePath $engine -ArgumentList $plan.EngineArguments `
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
