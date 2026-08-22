[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
function Assert-LiveAudioSessionWindowsOnly([bool]$windowsHost) {
  if (-not $windowsHost) { throw 'Live Windows audio-session probe is Windows-only.' }
}

function Get-LiveAudioSessionPlan([string]$repoRoot) {
  if ([string]::IsNullOrWhiteSpace($repoRoot)) { throw 'Live audio-session plan requires a repository root.' }

  $buildRoot = Join-Path $repoRoot '.local/live-audio-session-build'
  [pscustomobject]@{
    BuildRoot = $buildRoot
    ConfigureArgs = @(
      '-S', $repoRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_TESTS=ON',
      '-DHIBIKI_BUILD_LIVE_PROBES=ON'
    )
    Target = 'hibiki_live_audio_session_probe'
    ProbeName = 'hibiki_live_audio_session_probe.exe'
    ProbePath = Join-Path $buildRoot 'tests/RelWithDebInfo/hibiki_live_audio_session_probe.exe'
  }
}

function Assert-LiveAudioSessionPlan([pscustomobject]$plan, [string]$repoRoot) {
  $expectedBuildRoot = Join-Path $repoRoot '.local/live-audio-session-build'
  $expectedConfigureArgs = @(
    '-S', $repoRoot,
    '-B', $expectedBuildRoot,
    '-DHIBIKI_BUILD_TESTS=ON',
    '-DHIBIKI_BUILD_LIVE_PROBES=ON'
  )
  $expectedTarget = 'hibiki_live_audio_session_probe'
  $expectedProbeName = 'hibiki_live_audio_session_probe.exe'
  $expectedProbePath = Join-Path $expectedBuildRoot 'tests/RelWithDebInfo/hibiki_live_audio_session_probe.exe'

  if ([IO.Path]::GetFullPath($plan.BuildRoot) -ne [IO.Path]::GetFullPath($expectedBuildRoot) -or
      (@($plan.ConfigureArgs) -join "`n") -ne ($expectedConfigureArgs -join "`n") -or
      $plan.Target -ne $expectedTarget -or
      $plan.ProbeName -ne $expectedProbeName -or
      [IO.Path]::GetFullPath($plan.ProbePath) -ne [IO.Path]::GetFullPath($expectedProbePath)) {
    throw 'Live audio-session plan mismatch.'
  }
}

$repo = Split-Path -Parent $PSScriptRoot
if ($SelfTest) {
  $windowsCaught = $false
  try {
    Assert-LiveAudioSessionWindowsOnly $false
  } catch {
    $windowsCaught = $_.Exception.Message -match 'Windows-only'
  }
  if (-not $windowsCaught) { throw 'Live audio-session self-test expected Windows-only rejection.' }

  $plan = Get-LiveAudioSessionPlan $repo
  Assert-LiveAudioSessionPlan $plan $repo

  $wrongTarget = Get-LiveAudioSessionPlan $repo
  $wrongTarget.Target = 'hibiki_wrong_probe'
  $targetCaught = $false
  try {
    Assert-LiveAudioSessionPlan $wrongTarget $repo
  } catch {
    $targetCaught = $_.Exception.Message -match 'plan mismatch'
  }
  if (-not $targetCaught) { throw 'Live audio-session self-test expected target mismatch rejection.' }

  $unsafePlan = Get-LiveAudioSessionPlan $repo
  $unsafePlan.BuildRoot = Join-Path $repo 'outside-live-audio-session'
  $unsafeCaught = $false
  try {
    Assert-LiveAudioSessionPlan $unsafePlan $repo
  } catch {
    $unsafeCaught = $_.Exception.Message -match 'plan mismatch'
  }
  if (-not $unsafeCaught) { throw 'Live audio-session self-test expected unsafe build-root rejection.' }

  $argsPlan = Get-LiveAudioSessionPlan $repo
  $argsPlan.ConfigureArgs = @($argsPlan.ConfigureArgs) + '-DHIBIKI_BUILD_ENGINE_PREVIEW=ON'
  $argsCaught = $false
  try {
    Assert-LiveAudioSessionPlan $argsPlan $repo
  } catch {
    $argsCaught = $_.Exception.Message -match 'plan mismatch'
  }
  if (-not $argsCaught) { throw 'Live audio-session self-test expected CMake argument rejection.' }

  Write-Output 'Live audio-session wrapper self-test passed (5 cases).'
  exit 0
}

Assert-LiveAudioSessionWindowsOnly $IsWindows

$plan = Get-LiveAudioSessionPlan $repo
$build = $plan.BuildRoot
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $build --config RelWithDebInfo --target $plan.Target --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = $plan.ProbePath
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live audio-session probe failed with exit code $LASTEXITCODE" }
