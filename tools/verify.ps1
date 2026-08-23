[CmdletBinding()]
param(
  [switch]$Clean,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-VerifyBuildPlan {
  param(
    [Parameter(Mandatory)]
    [string]$RepositoryRoot
  )

  $relativeBuildRoot = '.local/build'
  $buildRoot = Join-Path $RepositoryRoot $relativeBuildRoot
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    RelativeBuildRoot = $relativeBuildRoot
    BuildRoot = $buildRoot
    ConfigureArguments = @(
      '-S', $RepositoryRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_TESTS=ON'
    )
    BuildArguments = @(
      '--build', $buildRoot,
      '--config', 'RelWithDebInfo',
      '--parallel'
    )
    TestArguments = @(
      '--test-dir', $buildRoot,
      '-C', 'RelWithDebInfo',
      '--output-on-failure'
    )
    Configuration = 'RelWithDebInfo'
  }
}

function Assert-VerifyBuildPlan {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Plan
  )

  if ($Plan.RelativeBuildRoot -ne '.local/build') {
    throw "Unexpected verification output root: $($Plan.RelativeBuildRoot)"
  }
  if ($Plan.BuildRoot -ne (Join-Path $Plan.RepositoryRoot $Plan.RelativeBuildRoot)) {
    throw 'Verification output root is not derived from the repository root.'
  }
  if ($Plan.Configuration -ne 'RelWithDebInfo') {
    throw "Unexpected verification configuration: $($Plan.Configuration)"
  }

  $configure = @($Plan.ConfigureArguments)
  if ($configure.Count -ne 5 -or
      $configure[0] -ne '-S' -or
      $configure[1] -ne $Plan.RepositoryRoot -or
      $configure[2] -ne '-B' -or
      $configure[3] -ne $Plan.BuildRoot -or
      $configure[4] -ne '-DHIBIKI_BUILD_TESTS=ON') {
    throw 'Verification configure arguments do not match the unsigned test contract.'
  }

  $build = @($Plan.BuildArguments)
  if ($build.Count -ne 5 -or
      $build[0] -ne '--build' -or
      $build[1] -ne $Plan.BuildRoot -or
      $build[2] -ne '--config' -or
      $build[3] -ne 'RelWithDebInfo' -or
      $build[4] -ne '--parallel') {
    throw 'Verification build arguments do not match the unsigned test contract.'
  }

  $test = @($Plan.TestArguments)
  if ($test.Count -ne 5 -or
      $test[0] -ne '--test-dir' -or
      $test[1] -ne $Plan.BuildRoot -or
      $test[2] -ne '-C' -or
      $test[3] -ne 'RelWithDebInfo' -or
      $test[4] -ne '--output-on-failure') {
    throw 'Verification test arguments do not match the unsigned test contract.'
  }
}

function Assert-VerifyCleanTarget {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Plan,
    [Parameter(Mandatory)]
    [System.IO.FileAttributes]$TargetAttributes,
    [Parameter(Mandatory)]
    [System.IO.FileAttributes]$ParentAttributes,
    [Parameter(Mandatory)]
    [bool]$TargetIsContainer
  )

  $expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $Plan.RepositoryRoot $Plan.RelativeBuildRoot))
  $actualBuildRoot = [System.IO.Path]::GetFullPath($Plan.BuildRoot)
  if ($actualBuildRoot -ne $expectedBuildRoot) {
    throw "Verify clean target does not match the repository-local build root: $actualBuildRoot"
  }
  if (-not $TargetIsContainer) {
    throw 'Verify clean target must be a directory.'
  }
  if (($TargetAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Verify clean target must not be a reparse point.'
  }
  if (($ParentAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Verify clean target parent must not be a reparse point.'
  }
}

function Get-VerifyExistingPath {
  param(
    [Parameter(Mandatory)]
    [string]$LiteralPath
  )

  try {
    return Get-Item -LiteralPath $LiteralPath -Force -ErrorAction Stop
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') {
      return $null
    }
    throw "Unable to inspect verify output path '$LiteralPath': $($_.Exception.Message)"
  }
}

function Assert-VerifyBuildTarget {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Plan,
    [Parameter(Mandatory)]
    [bool]$TargetExists,
    [Parameter(Mandatory)]
    [System.IO.FileAttributes]$TargetAttributes,
    [Parameter(Mandatory)]
    [bool]$TargetIsContainer,
    [Parameter(Mandatory)]
    [bool]$ParentExists,
    [Parameter(Mandatory)]
    [System.IO.FileAttributes]$ParentAttributes,
    [Parameter(Mandatory)]
    [bool]$ParentIsContainer
  )

  $expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $Plan.RepositoryRoot $Plan.RelativeBuildRoot))
  $actualBuildRoot = [System.IO.Path]::GetFullPath($Plan.BuildRoot)
  if ($actualBuildRoot -ne $expectedBuildRoot) {
    throw "Verify build target does not match the repository-local build root: $actualBuildRoot"
  }
  if ($ParentExists) {
    if (-not $ParentIsContainer) {
      throw 'Verify build target parent must be a directory.'
    }
    if (($ParentAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw 'Verify build target parent must not be a reparse point.'
    }
  }
  if ($TargetExists) {
    if (-not $TargetIsContainer) {
      throw 'Verify build target must be a directory.'
    }
    if (($TargetAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw 'Verify build target must not be a reparse point.'
    }
  }
}

function Invoke-MockVerifyExecution {
  param(
    [hashtable]$Mocks
  )
  # Mocks: @{ GetCommand = { param($Name) ... }; ConfigureExitCode = 0; BuildExitCode = 0; TestExitCode = 0 }
  $getCommand = $Mocks['GetCommand']
  if (-not (& $getCommand 'cmake')) { throw 'cmake not found' }
  if ($Mocks['ConfigureExitCode'] -ne 0) { throw "CMake configure failed with exit code $($Mocks['ConfigureExitCode'])" }
  if ($Mocks['BuildExitCode'] -ne 0) { throw "CMake build failed with exit code $($Mocks['BuildExitCode'])" }
  if ($Mocks['TestExitCode'] -ne 0) { throw "CTest failed with exit code $($Mocks['TestExitCode'])" }
  return $true
}

function Invoke-VerifySelfTest {
  $plan = Get-VerifyBuildPlan -RepositoryRoot $repo
  Assert-VerifyBuildPlan -Plan $plan

  Assert-VerifyBuildTarget -Plan $plan `
    -TargetExists $false `
    -TargetAttributes ([System.IO.FileAttributes]::Normal) `
    -TargetIsContainer $true `
    -ParentExists $false `
    -ParentAttributes ([System.IO.FileAttributes]::Normal) `
    -ParentIsContainer $true

  Assert-VerifyBuildTarget -Plan $plan `
    -TargetExists $false `
    -TargetAttributes ([System.IO.FileAttributes]::Normal) `
    -TargetIsContainer $true `
    -ParentExists $true `
    -ParentAttributes ([System.IO.FileAttributes]::Directory) `
    -ParentIsContainer $true

  Assert-VerifyBuildTarget -Plan $plan `
    -TargetExists $true `
    -TargetAttributes ([System.IO.FileAttributes]::Directory) `
    -TargetIsContainer $true `
    -ParentExists $true `
    -ParentAttributes ([System.IO.FileAttributes]::Directory) `
    -ParentIsContainer $true

  $outsideBuildTarget = $plan.PSObject.Copy()
  $outsideBuildTarget.BuildRoot = Join-Path $repo 'outside-verify-build'
  try {
    Assert-VerifyBuildTarget -Plan $outsideBuildTarget `
      -TargetExists $false `
      -TargetAttributes ([System.IO.FileAttributes]::Normal) `
      -TargetIsContainer $true `
      -ParentExists $false `
      -ParentAttributes ([System.IO.FileAttributes]::Normal) `
      -ParentIsContainer $true
    throw 'Outside build target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'does not match the repository-local build root') { throw }
  }

  try {
    Assert-VerifyBuildTarget -Plan $plan `
      -TargetExists $true `
      -TargetAttributes ([System.IO.FileAttributes]::Normal) `
      -TargetIsContainer $false `
      -ParentExists $true `
      -ParentAttributes ([System.IO.FileAttributes]::Directory) `
      -ParentIsContainer $true
    throw 'File build target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'target must be a directory') { throw }
  }

  try {
    Assert-VerifyBuildTarget -Plan $plan `
      -TargetExists $true `
      -TargetAttributes ([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) `
      -TargetIsContainer $true `
      -ParentExists $true `
      -ParentAttributes ([System.IO.FileAttributes]::Directory) `
      -ParentIsContainer $true
    throw 'Reparse-point build target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'target must not be a reparse point') { throw }
  }

  try {
    Assert-VerifyBuildTarget -Plan $plan `
      -TargetExists $false `
      -TargetAttributes ([System.IO.FileAttributes]::Normal) `
      -TargetIsContainer $true `
      -ParentExists $true `
      -ParentAttributes ([System.IO.FileAttributes]::Normal) `
      -ParentIsContainer $false
    throw 'File build parent was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'parent must be a directory') { throw }
  }

  try {
    Assert-VerifyBuildTarget -Plan $plan `
      -TargetExists $false `
      -TargetAttributes ([System.IO.FileAttributes]::Normal) `
      -TargetIsContainer $true `
      -ParentExists $true `
      -ParentAttributes ([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) `
      -ParentIsContainer $true
    throw 'Reparse-point build parent was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'parent must not be a reparse point') { throw }
  }

  Assert-VerifyCleanTarget -Plan $plan `
    -TargetAttributes ([System.IO.FileAttributes]::Directory) `
    -ParentAttributes ([System.IO.FileAttributes]::Directory) `
    -TargetIsContainer $true

  $outsideCleanTarget = $plan.PSObject.Copy()
  $outsideCleanTarget.BuildRoot = Join-Path $repo 'outside-verify-build'
  try {
    Assert-VerifyCleanTarget -Plan $outsideCleanTarget `
      -TargetAttributes ([System.IO.FileAttributes]::Directory) `
      -ParentAttributes ([System.IO.FileAttributes]::Directory) `
      -TargetIsContainer $true
    throw 'Outside clean target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'does not match the repository-local build root') { throw }
  }

  try {
    Assert-VerifyCleanTarget -Plan $plan `
      -TargetAttributes ([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) `
      -ParentAttributes ([System.IO.FileAttributes]::Directory) `
      -TargetIsContainer $true
    throw 'Reparse-point clean target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'target must not be a reparse point') { throw }
  }

  try {
    Assert-VerifyCleanTarget -Plan $plan `
      -TargetAttributes ([System.IO.FileAttributes]::Directory) `
      -ParentAttributes ([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) `
      -TargetIsContainer $true
    throw 'Reparse-point clean parent was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'parent must not be a reparse point') { throw }
  }

  try {
    Assert-VerifyCleanTarget -Plan $plan `
      -TargetAttributes ([System.IO.FileAttributes]::Normal) `
      -ParentAttributes ([System.IO.FileAttributes]::Directory) `
      -TargetIsContainer $false
    throw 'File clean target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'target must be a directory') { throw }
  }

  $invalidRoot = $plan.PSObject.Copy()
  $invalidRoot.RelativeBuildRoot = '.local/build-output'
  try {
    Assert-VerifyBuildPlan -Plan $invalidRoot
    throw 'Invalid output root was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'Unexpected verification output root') { throw }
  }

  $invalidConfigure = $plan.PSObject.Copy()
  $invalidConfigure.ConfigureArguments = @('-S', $repo, '-B', $plan.BuildRoot, '-DHIBIKI_BUILD_TESTS=OFF')
  try {
    Assert-VerifyBuildPlan -Plan $invalidConfigure
    throw 'Invalid configure flag was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'configure arguments') { throw }
  }

  $invalidBuild = $plan.PSObject.Copy()
  $invalidBuild.BuildArguments = @('--build', $plan.BuildRoot, '--config', 'Debug', '--parallel')
  try {
    Assert-VerifyBuildPlan -Plan $invalidBuild
    throw 'Invalid build configuration was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'build arguments') { throw }
  }

  $invalidTest = $plan.PSObject.Copy()
  $invalidTest.TestArguments = @('--test-dir', $plan.BuildRoot, '-C', 'Debug', '--output-on-failure')
  try {
    Assert-VerifyBuildPlan -Plan $invalidTest
    throw 'Invalid test configuration was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'test arguments') { throw }
  }

  # Mocked execution path: missing cmake
  try {
    Invoke-MockVerifyExecution -Mocks @{ GetCommand = { param($n) $null }; ConfigureExitCode = 0; BuildExitCode = 0; TestExitCode = 0 }
    throw 'Missing cmake was accepted.'
  } catch {
    if ($_.Exception.Message -notmatch 'cmake not found') { throw }
  }

  # Mocked execution path: cmake configure failure
  try {
    Invoke-MockVerifyExecution -Mocks @{ GetCommand = { param($n) [pscustomobject]@{Name=$n} }; ConfigureExitCode = 1; BuildExitCode = 0; TestExitCode = 0 }
    throw 'Configure failure was accepted.'
  } catch {
    if ($_.Exception.Message -notmatch 'CMake configure failed') { throw }
  }

  # Mocked execution path: cmake build failure
  try {
    Invoke-MockVerifyExecution -Mocks @{ GetCommand = { param($n) [pscustomobject]@{Name=$n} }; ConfigureExitCode = 0; BuildExitCode = 2; TestExitCode = 0 }
    throw 'Build failure was accepted.'
  } catch {
    if ($_.Exception.Message -notmatch 'CMake build failed') { throw }
  }

  # Mocked execution path: ctest failure
  try {
    Invoke-MockVerifyExecution -Mocks @{ GetCommand = { param($n) [pscustomobject]@{Name=$n} }; ConfigureExitCode = 0; BuildExitCode = 0; TestExitCode = 3 }
    throw 'CTest failure was accepted.'
  } catch {
    if ($_.Exception.Message -notmatch 'CTest failed') { throw }
  }

  # Mocked execution path: success
  $ok = Invoke-MockVerifyExecution -Mocks @{ GetCommand = { param($n) [pscustomobject]@{Name=$n} }; ConfigureExitCode = 0; BuildExitCode = 0; TestExitCode = 0 }
  if (-not $ok) { throw 'Mocked success did not return true.' }

  Write-Output 'Aggregate verification self-test passed (22 cases; offline/no-build/no-process/no-CMake/no-CTest/no-delete/no-file-write).'
}

if ($SelfTest) {
  Invoke-VerifySelfTest
  exit 0
}

$plan = Get-VerifyBuildPlan -RepositoryRoot $repo
$build = $plan.BuildRoot
if ($Clean -and (Test-Path -LiteralPath $build)) {
  $cleanTarget = Get-Item -LiteralPath $build -Force
  $cleanParent = Get-Item -LiteralPath (Split-Path -Parent $build) -Force
  Assert-VerifyCleanTarget -Plan $plan `
    -TargetAttributes $cleanTarget.Attributes `
    -ParentAttributes $cleanParent.Attributes `
    -TargetIsContainer $cleanTarget.PSIsContainer
  Remove-Item -LiteralPath $build -Recurse -Force
}
$target = Get-VerifyExistingPath -LiteralPath $build
$parent = Get-VerifyExistingPath -LiteralPath (Split-Path -Parent $build)
$targetExists = $null -ne $target
$targetAttributes = [System.IO.FileAttributes]::Normal
$targetIsContainer = $true
if ($targetExists) {
  $targetAttributes = $target.Attributes
  $targetIsContainer = $target.PSIsContainer
}
$parentExists = $null -ne $parent
$parentAttributes = [System.IO.FileAttributes]::Normal
$parentIsContainer = $true
if ($parentExists) {
  $parentAttributes = $parent.Attributes
  $parentIsContainer = $parent.PSIsContainer
}
Assert-VerifyBuildTarget -Plan $plan `
  -TargetExists $targetExists `
  -TargetAttributes $targetAttributes `
  -TargetIsContainer $targetIsContainer `
  -ParentExists $parentExists `
  -ParentAttributes $parentAttributes `
  -ParentIsContainer $parentIsContainer
New-Item -ItemType Directory -Path $build -Force | Out-Null

cmake @($plan.ConfigureArguments)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake @($plan.BuildArguments)
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
ctest @($plan.TestArguments)
if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }
Write-Output 'Hibiki unsigned local verification passed.'
