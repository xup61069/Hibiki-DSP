[CmdletBinding()]
param(
  [switch]$WriteTest,
  # Keep a broker-only diagnostic available; the default path exercises the
  # Engine Preview write-through loop.
  [switch]$DirectBroker,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
function Assert-LiveSystemVolumeWriteTestOptIn([bool]$writeTest, [bool]$selfTest) {
  if (-not $writeTest -and -not $selfTest) {
    throw 'This opt-in probe writes a temporary -3 dB change and restores it. Re-run with -WriteTest to confirm.'
  }
}

function Get-LiveSystemVolumePlan([string]$repoRoot, [bool]$directBroker) {
  if ([string]::IsNullOrWhiteSpace($repoRoot)) { throw 'Live system-volume plan requires a repository root.' }

  $buildRoot = Join-Path $repoRoot '.local/live-system-volume'
  $configureArgs = @(
    '-S', $repoRoot,
    '-B', $buildRoot,
    '-DHIBIKI_BUILD_TESTS=ON',
    '-DHIBIKI_BUILD_LIVE_PROBES=ON',
    '-DHIBIKI_BUILD_ENGINE_PREVIEW=OFF'
  )
  $target = if ($directBroker) { 'hibiki_live_system_volume_probe' } else { 'hibiki_live_engine_system_volume_probe' }
  $probeName = if ($directBroker) { 'hibiki_live_system_volume_probe.exe' } else { 'hibiki_live_engine_system_volume_probe.exe' }
  $engineArguments = if ($directBroker) { @() } else { @('--enable-system-volume') }

  [pscustomobject]@{
    DirectBroker = $directBroker
    BuildRoot = $buildRoot
    ConfigureArgs = $configureArgs
    Target = $target
    ProbeName = $probeName
    ProbePath = Join-Path $buildRoot "tests/RelWithDebInfo/$probeName"
    EngineBuildScript = if ($directBroker) { $null } else { Join-Path $repoRoot 'tools/build-engine-preview.ps1' }
    EnginePath = if ($directBroker) { $null } else { Join-Path $repoRoot '.local/engine-preview/Release/hibiki_engine_preview.exe' }
    EngineArguments = $engineArguments
  }
}

function Assert-LiveSystemVolumePlan([pscustomobject]$plan, [string]$repoRoot, [bool]$directBroker) {
  $expectedBuildRoot = Join-Path $repoRoot '.local/live-system-volume'
  $expectedConfigureArgs = @(
    '-S', $repoRoot,
    '-B', $expectedBuildRoot,
    '-DHIBIKI_BUILD_TESTS=ON',
    '-DHIBIKI_BUILD_LIVE_PROBES=ON',
    '-DHIBIKI_BUILD_ENGINE_PREVIEW=OFF'
  )
  $expectedTarget = if ($directBroker) { 'hibiki_live_system_volume_probe' } else { 'hibiki_live_engine_system_volume_probe' }
  $expectedProbeName = if ($directBroker) { 'hibiki_live_system_volume_probe.exe' } else { 'hibiki_live_engine_system_volume_probe.exe' }
  $expectedProbePath = Join-Path $expectedBuildRoot "tests/RelWithDebInfo/$expectedProbeName"
  $expectedEngineBuildScript = if ($directBroker) { $null } else { Join-Path $repoRoot 'tools/build-engine-preview.ps1' }
  $expectedEnginePath = if ($directBroker) { $null } else { Join-Path $repoRoot '.local/engine-preview/Release/hibiki_engine_preview.exe' }
  $expectedEngineArguments = if ($directBroker) { @() } else { @('--enable-system-volume') }

  if ([IO.Path]::GetFullPath($plan.BuildRoot) -ne [IO.Path]::GetFullPath($expectedBuildRoot) -or
      (@($plan.ConfigureArgs) -join "`n") -ne ($expectedConfigureArgs -join "`n") -or
      $plan.Target -ne $expectedTarget -or
      $plan.ProbeName -ne $expectedProbeName -or
      [IO.Path]::GetFullPath($plan.ProbePath) -ne [IO.Path]::GetFullPath($expectedProbePath) -or
      $plan.EngineBuildScript -ne $expectedEngineBuildScript -or
      $plan.EnginePath -ne $expectedEnginePath -or
      (@($plan.EngineArguments) -join "`n") -ne ($expectedEngineArguments -join "`n")) {
    throw "Live system-volume plan mismatch for directBroker=$directBroker."
  }
}

$repo = Split-Path -Parent $PSScriptRoot
if ($SelfTest) {
  $guardCaught = $false
  try {
    Assert-LiveSystemVolumeWriteTestOptIn $false $false
  } catch {
    $guardCaught = $_.Exception.Message -match 'Re-run with -WriteTest'
  }
  if (-not $guardCaught) { throw 'Live system-volume self-test expected WriteTest opt-in rejection.' }
  Assert-LiveSystemVolumeWriteTestOptIn $false $true

  $directPlan = Get-LiveSystemVolumePlan $repo $true
  Assert-LiveSystemVolumePlan $directPlan $repo $true
  $enginePlan = Get-LiveSystemVolumePlan $repo $false
  Assert-LiveSystemVolumePlan $enginePlan $repo $false

  $mismatchCaught = $false
  try {
    Assert-LiveSystemVolumePlan $directPlan $repo $false
  } catch {
    $mismatchCaught = $_.Exception.Message -match 'plan mismatch'
  }
  if (-not $mismatchCaught) { throw 'Live system-volume self-test expected direct/engine plan mismatch rejection.' }

  $unsafePlan = Get-LiveSystemVolumePlan $repo $true
  $unsafePlan.BuildRoot = Join-Path $repo 'outside-live-system-volume'
  $unsafeCaught = $false
  try {
    Assert-LiveSystemVolumePlan $unsafePlan $repo $true
  } catch {
    $unsafeCaught = $_.Exception.Message -match 'plan mismatch'
  }
  if (-not $unsafeCaught) { throw 'Live system-volume self-test expected unsafe build-root rejection.' }

  Write-Output 'Live system-volume wrapper self-test passed (6 cases).'
  exit 0
}

Assert-LiveSystemVolumeWriteTestOptIn $WriteTest.IsPresent $false

$plan = Get-LiveSystemVolumePlan $repo $DirectBroker.IsPresent
$build = $plan.BuildRoot
$configureArgs = @($plan.ConfigureArgs)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe configure failed: $LASTEXITCODE" }
$target = $plan.Target
& cmake --build $build --config RelWithDebInfo --target $target -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe build failed: $LASTEXITCODE" }

$probe = $plan.ProbePath
if (-not (Test-Path -LiteralPath $probe)) { throw "Live system-volume probe was not produced: $probe" }
if ($DirectBroker) {
  & $probe
  if ($LASTEXITCODE -ne 0) { throw "Live system-volume probe failed: $LASTEXITCODE" }
  Write-Output 'Live broker-only system-volume probe completed; any temporary attenuation was restored.'
  exit 0
}

$engineBuild = $plan.EngineBuildScript
& $engineBuild
if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
$engine = $plan.EnginePath
if (-not (Test-Path -LiteralPath $engine)) { throw "Engine Preview executable missing: $engine" }
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this live probe.'
}
$engineProcess = Start-Process -FilePath $engine -ArgumentList $plan.EngineArguments `
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
