#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

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

function Test-LiveProcessLoopbackPathUnderRoot {
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

function Get-LiveProcessLoopbackExistingAttributes {
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
    throw "Live process-loopback path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Live process-loopback path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-LiveProcessLoopbackPath {
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
  if (-not (Test-LiveProcessLoopbackPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Live process-loopback path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-LiveProcessLoopbackExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Live process-loopback $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-LiveProcessLoopbackExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Live process-loopback path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Live process-loopback path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Live process-loopback path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Live process-loopback path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Live process-loopback path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-LiveProcessLoopbackPathSelfTest {
  $repositoryRoot = [IO.Path]::GetFullPath('C:\hibiki-live-process-loopback-selftest').TrimEnd('\', '/')
  $fixture = Get-LiveProcessLoopbackPlan -RepoRoot $repositoryRoot
  $localRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.local')).TrimEnd('\', '/')
  $buildRoot = [IO.Path]::GetFullPath($fixture.BuildRoot).TrimEnd('\', '/')
  $probePath = [IO.Path]::GetFullPath($fixture.ProbePath).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-LiveProcessLoopbackPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveProcessLoopbackPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
    $localRoot = $directory
    $buildRoot = $directory
  }
  $cases++
  Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
    $buildRoot = $directory
    $probePath = $file
  }
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path $repositoryRoot 'outside-live-process-loopback'
  try { Assert-LiveProcessLoopbackPath -Path $outsidePath -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Live process-loopback self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $buildRoot = $directory
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Live process-loopback self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Live process-loopback self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $buildRoot = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Live process-loopback self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Live process-loopback self-test expected a non-file rejection.' }
  $cases++

  $nonDirectoryParentCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $file
      $probePath = $file
    }
  } catch { $nonDirectoryParentCaught = $_.Exception.Message -match 'parent is not a directory' }
  if (-not $nonDirectoryParentCaught) { throw 'Live process-loopback self-test expected a non-directory-parent rejection.' }
  $cases++

  $missingProbeCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
    }
  } catch { $missingProbeCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingProbeCaught) { throw 'Live process-loopback self-test expected a missing-probe rejection.' }
  $cases++

  $inspectionCaught = $false
  try {
    Assert-LiveProcessLoopbackPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf -SyntheticInspectionErrors @{
      $probePath = 'synthetic access denied'
    }
  } catch { $inspectionCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $inspectionCaught) { throw 'Live process-loopback self-test expected an inspection-failure rejection.' }
  $cases++

  return $cases
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

  $caseCount += Invoke-LiveProcessLoopbackPathSelfTest
  Write-Output "Live process-loopback wrapper self-test passed ($caseCount cases; offline/no-cmake/no-probe/no-file-write)."
  exit 0
}

Assert-LiveProcessLoopbackWindowsOnly -WindowsHost ([bool]$IsWindows)
$plan = Get-LiveProcessLoopbackPlan -RepoRoot $repo
Assert-LiveProcessLoopbackPlan -Plan $plan -RepoRoot $repo
Assert-LiveProcessLoopbackPath -Path $plan.BuildRoot -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf
Assert-LiveProcessLoopbackPath -Path $plan.ProbePath -Root $plan.BuildRoot -Kind File -AllowMissingLeaf
New-Item -ItemType Directory -Path $plan.BuildRoot -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $plan.BuildRoot --config RelWithDebInfo --target $plan.Target --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
Assert-LiveProcessLoopbackPath -Path $plan.BuildRoot -Root (Join-Path $repo '.local') -Kind Directory
$probe = $plan.ProbePath
Assert-LiveProcessLoopbackPath -Path $probe -Root $plan.BuildRoot -Kind File
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live process-loopback probe failed with exit code $LASTEXITCODE" }
