#Requires -Version 7
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

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
function Assert-LiveSessionVolumeOptIn([bool]$windowsHost, [bool]$writeTest, [bool]$selfTest) {
    if (-not $windowsHost) { throw 'Live Windows session-volume probe is Windows-only.' }
    if (-not $writeTest -and -not $selfTest) {
        throw 'No session-volume write was performed. Re-run with -WriteTest to create a silent test session, attenuate it, read it back, and restore it.'
    }
}

function Assert-LiveSessionVolumeProbeExitContract([string]$repoRoot) {
    if ([string]::IsNullOrWhiteSpace($repoRoot)) {
        throw 'Live session-volume probe exit contract requires a repository root.'
    }
    $probePaths = @(
        (Join-Path $repoRoot 'tests/live_session_volume_probe.cpp'),
        (Join-Path $repoRoot 'tests/live_engine_session_volume_probe.cpp')
    )
    foreach ($probePath in $probePaths) {
        if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
            throw "Live session-volume probe source is missing: $probePath"
        }
        $lines = @(Get-Content -LiteralPath $probePath)
        $source = $lines -join "`n"
        if ($source -notmatch 'return\s+passed\s+\|\|\s+unavailable\s+\?\s+0\s*:\s*1;') {
            throw "Live session-volume probe has no fail-closed unavailable exit contract: $probePath"
        }

        $writeLine = -1
        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -match '^\s*restore_required\s*=\s*true;') {
                $writeLine = $lineIndex
                break
            }
        }
        if ($writeLine -lt 0) {
            throw "Live session-volume probe has no restore arming before mutation: $probePath"
        }

        for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -notmatch 'session_volume(?:_engine)?_live=unavailable') {
                continue
            }
            if ($lines[$lineIndex] -match 'reason=com-init') {
                $windowEnd = [Math]::Min($lines.Count - 1, $lineIndex + 4)
                $window = $lines[$lineIndex..$windowEnd] -join "`n"
                if ($window -notmatch 'return\s+0;') {
                    throw "COM initialization unavailability must exit successfully: $probePath"
                }
                continue
            }

            $markedUnavailable = $false
            $reachedBreak = $false
            $windowEnd = [Math]::Min($lines.Count - 1, $lineIndex + 24)
            for ($nextLine = $lineIndex + 1; $nextLine -le $windowEnd; $nextLine++) {
                if ($lines[$nextLine] -match '^\s*unavailable\s*=\s*true;') {
                    $markedUnavailable = $true
                }
                if ($lines[$nextLine] -match '^\s*break;') {
                    $reachedBreak = $true
                    break
                }
            }
            if (-not $markedUnavailable -or -not $reachedBreak) {
                throw "Pre-write unavailability must be marked before its exit: ${probePath}:$($lineIndex + 1)"
            }
        }

        for ($lineIndex = $writeLine + 1; $lineIndex -lt $lines.Count; $lineIndex++) {
            if ($lines[$lineIndex] -match '^\s*unavailable\s*=\s*true;') {
                throw "Post-write failure path must not be downgraded to unavailable: ${probePath}:$($lineIndex + 1)"
            }
        }
    }
}

function Assert-LiveSessionVolumePipeIoContract([string]$repoRoot) {
    if ([string]::IsNullOrWhiteSpace($repoRoot)) {
        throw 'Live session-volume pipe I/O contract requires a repository root.'
    }
    $probePath = Join-Path $repoRoot 'tests/live_engine_session_volume_probe.cpp'
    if (-not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
        throw "Live Engine session-volume probe source is missing: $probePath"
    }
    $source = Get-Content -LiteralPath $probePath -Raw
    $requiredPatterns = @(
        'FILE_ATTRIBUTE_NORMAL\s*\|\s*FILE_FLAG_OVERLAPPED',
        'OVERLAPPED\s+overlapped\s*\{\}',
        'WaitForSingleObject\(event,\s*kIoTimeoutMs\)',
        'CancelIoEx\(handle_,\s*&overlapped\)',
        'CancelIoEx\(handle_,\s*nullptr\)',
        '!transfer\(true,\s*const_cast<std::uint8_t\*>\(encoded\.data\(\)\),\s*encoded\.size\(\)\)\)\s*\{\s*close\(\);\s*return\s+std::nullopt;',
        'if\s*\(!transfer\(false,\s*length\.data\(\),\s*length\.size\(\)\)\)\s*\{\s*close\(\);',
        'if\s*\(response_bytes\s*<\s*20U.*?\)\s*\{\s*close\(\);',
        'if\s*\(!transfer\(false,\s*response\.data\(\),\s*response\.size\(\)\)\)\s*\{\s*close\(\);',
        'if\s*\(!decoded\.has_value\(\).*?\)\s*\{\s*close\(\);',
        'bool\s+connected\(\)\s+const\s+noexcept\s*\{\s*return\s+handle_\s*!=\s*INVALID_HANDLE_VALUE;'
    )
    foreach ($pattern in $requiredPatterns) {
        if ($source -notmatch $pattern) {
            throw "Live Engine session-volume pipe I/O contract is missing: $pattern"
        }
    }
    $disconnectStops = ([regex]::Matches($source, 'if\s*\(!pipe\.connected\(\)\)\s*return false;')).Count
    if ($disconnectStops -ne 4) {
        throw "Live Engine session-volume wait helpers must stop after disconnect (found $disconnectStops)."
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

function Test-LiveSessionVolumePathUnderRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $fullPath -eq $fullRoot -or $fullPath.StartsWith(
        $fullRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}

function Get-LiveSessionVolumeExistingAttributes {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [hashtable]$SyntheticAttributes,
        [hashtable]$SyntheticInspectionErrors
    )
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
        return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
    }
    if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
        throw "Live session-volume path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
    }
    try {
        return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
    }
    catch {
        if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
        throw "Live session-volume path inspection failed: $fullPath ($($_.Exception.Message))"
    }
}

function Assert-LiveSessionVolumePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][ValidateSet('File', 'Directory')][string]$Kind,
        [switch]$AllowMissingLeaf,
        [hashtable]$SyntheticAttributes,
        [hashtable]$SyntheticInspectionErrors
    )
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not (Test-LiveSessionVolumePathUnderRoot -Path $fullPath -Root $fullRoot)) {
        throw "Live session-volume path must remain under the expected root: $fullPath"
    }

    $leafAttributes = Get-LiveSessionVolumeExistingAttributes -Path $fullPath `
        -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
        throw "Live session-volume $Kind does not exist: $fullPath"
    }

    $cursor = $fullPath
    while ($true) {
        $attributes = Get-LiveSessionVolumeExistingAttributes -Path $cursor `
            -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
        if ($null -ne $attributes) {
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Live session-volume path or parent is a reparse point: $cursor"
            }
            if ($cursor -eq $fullPath) {
                if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
                    throw "Live session-volume path is not a directory: $fullPath"
                }
                if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
                    throw "Live session-volume path is not a file: $fullPath"
                }
            } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
                throw "Live session-volume path parent is not a directory: $cursor"
            }
        }
        if ($cursor -eq $fullRoot) { break }
        $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
            throw "Live session-volume path could not reach the expected root: $fullPath"
        }
        $cursor = $parent
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
    Assert-LiveSessionVolumeProbeExitContract $repo
    Assert-LiveSessionVolumePipeIoContract $repo

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

    $synthetic = @{}
    Assert-LiveSessionVolumePath -Path (Join-Path $repo '.local/missing-leaf') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes $synthetic
    $caseCount = 10

    $outsideCaught = $false
    try {
        Assert-LiveSessionVolumePath -Path 'C:/hibiki-outside-session-volume' -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
    } catch {
        $outsideCaught = $_.Exception.Message -match 'must remain under the expected root'
    }
    if (-not $outsideCaught) { throw 'Live session-volume self-test expected outside-root rejection.' }
    $caseCount++

    $reparseCaught = $false
    try {
        Assert-LiveSessionVolumePath -Path (Join-Path $repo '.local/reparse-child') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/reparse-child')).TrimEnd('\', '/')) = [System.IO.FileAttributes]([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) }
    } catch {
        $reparseCaught = $_.Exception.Message -match 'reparse point'
    }
    if (-not $reparseCaught) { throw 'Live session-volume self-test expected reparse-point rejection.' }
    $caseCount++

    $wrongKindCaught = $false
    try {
        Assert-LiveSessionVolumePath -Path (Join-Path $repo '.local/file-as-dir') -Root (Join-Path $repo '.local') -Kind Directory -SyntheticAttributes @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/file-as-dir')).TrimEnd('\', '/')) = [System.IO.FileAttributes]::Archive }
    } catch {
        $wrongKindCaught = $_.Exception.Message -match 'is not a directory'
    }
    if (-not $wrongKindCaught) { throw 'Live session-volume self-test expected wrong-kind rejection.' }
    $caseCount++

    $inspectionCaught = $false
    try {
        Assert-LiveSessionVolumePath -Path (Join-Path $repo '.local/inspection-child') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticInspectionErrors @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/inspection-child')).TrimEnd('\', '/')) = 'synthetic access denied' }
    } catch {
        $inspectionCaught = $_.Exception.Message -match 'path inspection failed'
    }
    if (-not $inspectionCaught) { throw 'Live session-volume self-test expected inspection-failure rejection.' }
    $caseCount++

    Write-Output "Live session-volume wrapper self-test passed ($caseCount cases)."
    exit 0
}

Assert-LiveSessionVolumeOptIn $IsWindows $WriteTest.IsPresent $false

$plan = Get-LiveSessionVolumePlan $repo $DirectCoordinator.IsPresent
$localRoot = Join-Path $repo '.local'
Assert-LiveSessionVolumePath -Path $plan.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf
Assert-LiveSessionVolumePath -Path $plan.ProbePath -Root $localRoot -Kind File -AllowMissingLeaf
if (-not $DirectCoordinator.IsPresent) {
    Assert-LiveSessionVolumePath -Path $plan.EnginePath -Root $localRoot -Kind File -AllowMissingLeaf
}
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
