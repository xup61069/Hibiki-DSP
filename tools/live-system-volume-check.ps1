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

function Test-LiveSystemVolumePathUnderRoot {
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

function Get-LiveSystemVolumeExistingAttributes {
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
    throw "Live system-volume path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Live system-volume path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-LiveSystemVolumePath {
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
  if (-not (Test-LiveSystemVolumePathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Live system-volume path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-LiveSystemVolumeExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Live system-volume $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-LiveSystemVolumeExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Live system-volume path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Live system-volume path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Live system-volume path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Live system-volume path parent is not a directory: $cursor"
      }
    }
    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Live system-volume path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
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

  $synthetic = @{}
  Assert-LiveSystemVolumePath -Path (Join-Path $repo '.local/missing-leaf') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes $synthetic
  $caseCount = 7

  $outsideCaught = $false
  try {
    Assert-LiveSystemVolumePath -Path 'C:/hibiki-outside-system-volume' -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  } catch {
    $outsideCaught = $_.Exception.Message -match 'must remain under the expected root'
  }
  if (-not $outsideCaught) { throw 'Live system-volume self-test expected outside-root rejection.' }
  $caseCount++

  $reparseCaught = $false
  try {
    Assert-LiveSystemVolumePath -Path (Join-Path $repo '.local/reparse-child') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/reparse-child')).TrimEnd('\', '/')) = [System.IO.FileAttributes]([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) }
  } catch {
    $reparseCaught = $_.Exception.Message -match 'reparse point'
  }
  if (-not $reparseCaught) { throw 'Live system-volume self-test expected reparse-point rejection.' }
  $caseCount++

  $wrongKindCaught = $false
  try {
    Assert-LiveSystemVolumePath -Path (Join-Path $repo '.local/file-as-dir') -Root (Join-Path $repo '.local') -Kind Directory -SyntheticAttributes @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/file-as-dir')).TrimEnd('\', '/')) = [System.IO.FileAttributes]::Archive }
  } catch {
    $wrongKindCaught = $_.Exception.Message -match 'is not a directory'
  }
  if (-not $wrongKindCaught) { throw 'Live system-volume self-test expected wrong-kind rejection.' }
  $caseCount++

  $inspectionCaught = $false
  try {
    Assert-LiveSystemVolumePath -Path (Join-Path $repo '.local/inspection-child') -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf -SyntheticInspectionErrors @{ ([IO.Path]::GetFullPath((Join-Path $repo '.local/inspection-child')).TrimEnd('\', '/')) = 'synthetic access denied' }
  } catch {
    $inspectionCaught = $_.Exception.Message -match 'path inspection failed'
  }
  if (-not $inspectionCaught) { throw 'Live system-volume self-test expected inspection-failure rejection.' }
  $caseCount++

  Write-Output "Live system-volume wrapper self-test passed ($caseCount cases)."
  exit 0
}

Assert-LiveSystemVolumeWriteTestOptIn $WriteTest.IsPresent $false

$plan = Get-LiveSystemVolumePlan $repo $DirectBroker.IsPresent
$localRoot = Join-Path $repo '.local'
Assert-LiveSystemVolumePath -Path $plan.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf
Assert-LiveSystemVolumePath -Path $plan.ProbePath -Root $localRoot -Kind File -AllowMissingLeaf
if (-not $DirectBroker.IsPresent) {
  Assert-LiveSystemVolumePath -Path $plan.EnginePath -Root $localRoot -Kind File -AllowMissingLeaf
}
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
