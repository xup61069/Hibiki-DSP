[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Assert-LiveProcessLoopbackWindowsOnly {
  param([bool]$WindowsHost)

  if (-not $WindowsHost) { throw 'Live process-loopback probe is Windows-only.' }
}

function Get-LiveProcessLoopbackPlan {
  param([string]$RepoRoot)

  $buildRoot = Join-Path $RepoRoot '.local/live-process-loopback-build'
  [pscustomobject]@{
    BuildRoot = $buildRoot
    ConfigureArgs = @(
      '-S', $RepoRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_TESTS=ON',
      '-DHIBIKI_BUILD_LIVE_PROBES=ON'
    )
    Target = 'hibiki_live_process_loopback_probe'
    ProbeName = 'hibiki_live_process_loopback_probe.exe'
    ProbePath = Join-Path $buildRoot 'tests/RelWithDebInfo/hibiki_live_process_loopback_probe.exe'
  }
}

function Assert-LiveProcessLoopbackPlan {
  param(
    [pscustomobject]$Plan,
    [string]$RepoRoot
  )

  $expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot '.local/live-process-loopback-build'))
  if ([System.IO.Path]::GetFullPath($Plan.BuildRoot) -ne $expectedBuildRoot) {
    throw "Live process-loopback self-test rejected unsafe build root: $($Plan.BuildRoot)"
  }

  $expectedArgs = @(
    '-S', $RepoRoot,
    '-B', $expectedBuildRoot,
    '-DHIBIKI_BUILD_TESTS=ON',
    '-DHIBIKI_BUILD_LIVE_PROBES=ON'
  )
  if ((@($Plan.ConfigureArgs) -join "`n") -ne ($expectedArgs -join "`n")) {
    throw 'Live process-loopback self-test rejected the CMake configure arguments.'
  }
  if ($Plan.Target -cne 'hibiki_live_process_loopback_probe') {
    throw 'Live process-loopback self-test rejected the CMake target.'
  }
  if ($Plan.ProbeName -cne 'hibiki_live_process_loopback_probe.exe') {
    throw 'Live process-loopback self-test rejected the probe executable name.'
  }
  $expectedProbePath = Join-Path $expectedBuildRoot 'tests/RelWithDebInfo/hibiki_live_process_loopback_probe.exe'
  if ([System.IO.Path]::GetFullPath($Plan.ProbePath) -ne [System.IO.Path]::GetFullPath($expectedProbePath)) {
    throw 'Live process-loopback self-test rejected the probe executable path.'
  }
}

if ($SelfTest) {
  $caseCount = 0

  $windowsCaught = $false
  try { Assert-LiveProcessLoopbackWindowsOnly -WindowsHost:$false } catch { $windowsCaught = $true }
  if (-not $windowsCaught) { throw 'Live process-loopback self-test expected Windows-only rejection.' }
  $caseCount++

  $plan = Get-LiveProcessLoopbackPlan -RepoRoot $repo
  Assert-LiveProcessLoopbackPlan -Plan $plan -RepoRoot $repo
  $caseCount++

  $targetCaught = $false
  $wrongTarget = Get-LiveProcessLoopbackPlan -RepoRoot $repo
  $wrongTarget.Target = 'hibiki_wrong_probe'
  try { Assert-LiveProcessLoopbackPlan -Plan $wrongTarget -RepoRoot $repo } catch { $targetCaught = $true }
  if (-not $targetCaught) { throw 'Live process-loopback self-test expected target mismatch rejection.' }
  $caseCount++

  $unsafeCaught = $false
  $unsafePlan = Get-LiveProcessLoopbackPlan -RepoRoot $repo
  $unsafePlan.BuildRoot = Join-Path $repo 'build'
  try { Assert-LiveProcessLoopbackPlan -Plan $unsafePlan -RepoRoot $repo } catch { $unsafeCaught = $true }
  if (-not $unsafeCaught) { throw 'Live process-loopback self-test expected unsafe build-root rejection.' }
  $caseCount++

  $argsCaught = $false
  $wrongArgs = Get-LiveProcessLoopbackPlan -RepoRoot $repo
  $wrongArgs.ConfigureArgs = @($wrongArgs.ConfigureArgs + '-DUNSAFE=ON')
  try { Assert-LiveProcessLoopbackPlan -Plan $wrongArgs -RepoRoot $repo } catch { $argsCaught = $true }
  if (-not $argsCaught) { throw 'Live process-loopback self-test expected CMake argument rejection.' }
  $caseCount++

  Write-Output "Live process-loopback wrapper self-test passed ($caseCount cases)."
  exit 0
}

Assert-LiveProcessLoopbackWindowsOnly -WindowsHost ([bool]$IsWindows)
$plan = Get-LiveProcessLoopbackPlan -RepoRoot $repo
Assert-LiveProcessLoopbackPlan -Plan $plan -RepoRoot $repo
New-Item -ItemType Directory -Path $plan.BuildRoot -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $plan.BuildRoot --config RelWithDebInfo --target $plan.Target --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = $plan.ProbePath
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live process-loopback probe failed with exit code $LASTEXITCODE" }
