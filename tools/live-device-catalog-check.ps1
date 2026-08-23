[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Assert-LiveDeviceCatalogWindowsOnly {
  param([bool]$WindowsHost)

  if (-not $WindowsHost) { throw 'Live device catalog probe is Windows-only.' }
}

function Get-LiveDeviceCatalogPlan {
  param([string]$RepoRoot)

  $buildRoot = Join-Path $RepoRoot '.local/live-device-catalog-build'
  [pscustomobject]@{
    BuildRoot = $buildRoot
    ConfigureArgs = @(
      '-S', $RepoRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_TESTS=ON',
      '-DHIBIKI_BUILD_LIVE_PROBES=ON'
    )
    Target = 'hibiki_live_device_catalog_probe'
    ProbeName = 'hibiki_live_device_catalog_probe.exe'
    ProbePath = Join-Path $buildRoot 'tests/RelWithDebInfo/hibiki_live_device_catalog_probe.exe'
  }
}

function Test-LiveDeviceCatalogPathUnderRoot {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath -eq $fullRoot -or $fullPath.StartsWith(
    $fullRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase
  )
}

function Get-LiveDeviceCatalogExistingAttributes {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Live device catalog path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Live device catalog path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-LiveDeviceCatalogPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-LiveDeviceCatalogPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Live device catalog path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-LiveDeviceCatalogExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Live device catalog $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-LiveDeviceCatalogExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Live device catalog path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Live device catalog path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Live device catalog path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Live device catalog path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [System.IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Live device catalog path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-LiveDeviceCatalogPathSelfTest {
  $repositoryRoot = [System.IO.Path]::GetFullPath('C:\hibiki-live-device-catalog-selftest').TrimEnd('\', '/')
  $fixture = Get-LiveDeviceCatalogPlan -RepoRoot $repositoryRoot
  $localRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot '.local')).TrimEnd('\', '/')
  $buildRoot = [System.IO.Path]::GetFullPath([string]$fixture.BuildRoot).TrimEnd('\', '/')
  $probePath = [System.IO.Path]::GetFullPath([string]$fixture.ProbePath).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-LiveDeviceCatalogPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveDeviceCatalogPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
    $localRoot = $directory
    $buildRoot = $directory
  }
  $cases++
  Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
    $buildRoot = $directory
    $probePath = $file
  }
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path $repositoryRoot 'outside-live-device-catalog'
  try { Assert-LiveDeviceCatalogPath -Path $outsidePath -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Live device catalog self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $buildRoot = $directory
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Live device catalog self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Live device catalog self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $buildRoot = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Live device catalog self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Live device catalog self-test expected a non-file rejection.' }
  $cases++

  $nonDirectoryParentCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $file
      $probePath = $file
    }
  } catch { $nonDirectoryParentCaught = $_.Exception.Message -match 'parent is not a directory' }
  if (-not $nonDirectoryParentCaught) { throw 'Live device catalog self-test expected a non-directory-parent rejection.' }
  $cases++

  $missingProbeCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
    }
  } catch { $missingProbeCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingProbeCaught) { throw 'Live device catalog self-test expected a missing-probe rejection.' }
  $cases++

  $leafInspectionErrorCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{ $buildRoot = $directory } `
      -SyntheticInspectionErrors @{ $probePath = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'Live device catalog self-test expected a leaf inspection-error rejection.' }
  $cases++

  $parentInspectionErrorCaught = $false
  try {
    Assert-LiveDeviceCatalogPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{} `
      -SyntheticInspectionErrors @{ $buildRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'Live device catalog self-test expected a parent inspection-error rejection.' }
  $cases++

  return $cases
}

function Assert-LiveDeviceCatalogPlan {
  param(
    [pscustomobject]$Plan,
    [string]$RepoRoot
  )

  $expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot '.local/live-device-catalog-build'))
  if ([System.IO.Path]::GetFullPath($Plan.BuildRoot) -ne $expectedBuildRoot) {
    throw "Live device catalog self-test rejected unsafe build root: $($Plan.BuildRoot)"
  }

  $expectedArgs = @(
    '-S', $RepoRoot,
    '-B', $expectedBuildRoot,
    '-DHIBIKI_BUILD_TESTS=ON',
    '-DHIBIKI_BUILD_LIVE_PROBES=ON'
  )
  if ((@($Plan.ConfigureArgs) -join "`n") -ne ($expectedArgs -join "`n")) {
    throw 'Live device catalog self-test rejected the CMake configure arguments.'
  }
  if ($Plan.Target -cne 'hibiki_live_device_catalog_probe') {
    throw 'Live device catalog self-test rejected the CMake target.'
  }
  if ($Plan.ProbeName -cne 'hibiki_live_device_catalog_probe.exe') {
    throw 'Live device catalog self-test rejected the probe executable name.'
  }
  $expectedProbePath = Join-Path $expectedBuildRoot 'tests/RelWithDebInfo/hibiki_live_device_catalog_probe.exe'
  if ([System.IO.Path]::GetFullPath($Plan.ProbePath) -ne [System.IO.Path]::GetFullPath($expectedProbePath)) {
    throw 'Live device catalog self-test rejected the probe executable path.'
  }
}

if ($SelfTest) {
  $caseCount = 0

  $windowsCaught = $false
  try { Assert-LiveDeviceCatalogWindowsOnly -WindowsHost:$false } catch { $windowsCaught = $true }
  if (-not $windowsCaught) { throw 'Live device catalog self-test expected Windows-only rejection.' }
  $caseCount++

  $plan = Get-LiveDeviceCatalogPlan -RepoRoot $repo
  Assert-LiveDeviceCatalogPlan -Plan $plan -RepoRoot $repo
  $caseCount++

  $targetCaught = $false
  $wrongTarget = Get-LiveDeviceCatalogPlan -RepoRoot $repo
  $wrongTarget.Target = 'hibiki_wrong_probe'
  try { Assert-LiveDeviceCatalogPlan -Plan $wrongTarget -RepoRoot $repo } catch { $targetCaught = $true }
  if (-not $targetCaught) { throw 'Live device catalog self-test expected target mismatch rejection.' }
  $caseCount++

  $unsafeCaught = $false
  $unsafePlan = Get-LiveDeviceCatalogPlan -RepoRoot $repo
  $unsafePlan.BuildRoot = Join-Path $repo 'build'
  try { Assert-LiveDeviceCatalogPlan -Plan $unsafePlan -RepoRoot $repo } catch { $unsafeCaught = $true }
  if (-not $unsafeCaught) { throw 'Live device catalog self-test expected unsafe build-root rejection.' }
  $caseCount++

  $argsCaught = $false
  $wrongArgs = Get-LiveDeviceCatalogPlan -RepoRoot $repo
  $wrongArgs.ConfigureArgs = @($wrongArgs.ConfigureArgs + '-DUNSAFE=ON')
  try { Assert-LiveDeviceCatalogPlan -Plan $wrongArgs -RepoRoot $repo } catch { $argsCaught = $true }
  if (-not $argsCaught) { throw 'Live device catalog self-test expected CMake argument rejection.' }
  $caseCount++

  Write-Output "Live device-catalog wrapper self-test passed ($caseCount cases)."
  $pathCases = Invoke-LiveDeviceCatalogPathSelfTest
  Write-Output "Live device-catalog path self-test: $pathCases cases passed (offline/no-cmake/no-probe/no-file-write)."
  exit 0
}

Assert-LiveDeviceCatalogWindowsOnly -WindowsHost ([bool]$IsWindows)
$plan = Get-LiveDeviceCatalogPlan -RepoRoot $repo
Assert-LiveDeviceCatalogPlan -Plan $plan -RepoRoot $repo
$localRoot = Join-Path $repo '.local'
Assert-LiveDeviceCatalogPath -Path $plan.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
$probe = $plan.ProbePath
Assert-LiveDeviceCatalogPath -Path $probe -Root $plan.BuildRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
New-Item -ItemType Directory -Path $plan.BuildRoot -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $plan.BuildRoot --config RelWithDebInfo --target $plan.Target --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
Assert-LiveDeviceCatalogPath -Path $plan.BuildRoot -Root $localRoot -Kind Directory
Assert-LiveDeviceCatalogPath -Path $probe -Root $plan.BuildRoot -Kind File
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live device catalog probe failed with exit code $LASTEXITCODE" }
