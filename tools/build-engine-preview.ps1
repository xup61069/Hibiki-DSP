[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-EnginePreviewBuildPlan {
  param(
    [Parameter(Mandatory)]
    [string]$RepositoryRoot
  )

  $relativeBuildRoot = '.local/engine-preview'
  $buildRoot = Join-Path $RepositoryRoot $relativeBuildRoot
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    RelativeBuildRoot = $relativeBuildRoot
    BuildRoot = $buildRoot
    ConfigureArguments = @(
      '-S', $RepositoryRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_ENGINE_PREVIEW=ON',
      '-DHIBIKI_BUILD_TESTS=OFF'
    )
    BuildArguments = @(
      '--build', $buildRoot,
      '--config', 'Release',
      '--target', 'hibiki_engine_preview'
    )
    Configuration = 'Release'
    Target = 'hibiki_engine_preview'
  }
}

function Test-EnginePreviewPathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-EnginePreviewExistingAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Engine Preview build path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Engine Preview build path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-EnginePreviewBuildRoot {
  param(
    [Parameter(Mandatory)][pscustomobject]$Plan,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $expectedRoot = [IO.Path]::GetFullPath((Join-Path $Plan.RepositoryRoot '.local')).TrimEnd('\', '/')
  $candidate = [IO.Path]::GetFullPath($Plan.BuildRoot).TrimEnd('\', '/')
  if (-not (Test-EnginePreviewPathUnderRoot -Path $candidate -Root $expectedRoot)) {
    throw "Engine Preview build root must remain under the repository .local root: $candidate"
  }

  $cursor = $candidate
  while ($true) {
    $attributes = Get-EnginePreviewExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Engine Preview build root or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $candidate -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Engine Preview build root is not a directory: $candidate"
      }
    }

    if ($cursor -eq $expectedRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Engine Preview build root could not reach the repository .local root: $candidate"
    }
    $cursor = $parent
  }
}

function Assert-EnginePreviewBuildPlan {
  param(
    [Parameter(Mandatory)]
    [pscustomobject]$Plan
  )

  if ($Plan.Target -ne 'hibiki_engine_preview') {
    throw "Unexpected Engine Preview target: $($Plan.Target)"
  }
  if ($Plan.Configuration -ne 'Release') {
    throw "Unexpected Engine Preview configuration: $($Plan.Configuration)"
  }
  if ($Plan.RelativeBuildRoot -ne '.local/engine-preview') {
    throw "Unexpected Engine Preview output root: $($Plan.RelativeBuildRoot)"
  }
  if ($Plan.BuildRoot -ne (Join-Path $Plan.RepositoryRoot $Plan.RelativeBuildRoot)) {
    throw 'Engine Preview output root is not derived from the repository root.'
  }

  $configure = @($Plan.ConfigureArguments)
  if ($configure.Count -ne 6 -or
      $configure[0] -ne '-S' -or
      $configure[1] -ne $Plan.RepositoryRoot -or
      $configure[2] -ne '-B' -or
      $configure[3] -ne $Plan.BuildRoot -or
      $configure[4] -ne '-DHIBIKI_BUILD_ENGINE_PREVIEW=ON' -or
      $configure[5] -ne '-DHIBIKI_BUILD_TESTS=OFF') {
    throw 'Engine Preview configure arguments do not match the bounded build contract.'
  }

  $build = @($Plan.BuildArguments)
  if ($build.Count -ne 6 -or
      $build[0] -ne '--build' -or
      $build[1] -ne $Plan.BuildRoot -or
      $build[2] -ne '--config' -or
      $build[3] -ne 'Release' -or
      $build[4] -ne '--target' -or
      $build[5] -ne 'hibiki_engine_preview') {
    throw 'Engine Preview build arguments do not match the bounded build contract.'
  }
}

function Invoke-EnginePreviewBuildSelfTest {
  $plan = Get-EnginePreviewBuildPlan -RepositoryRoot $repo
  Assert-EnginePreviewBuildPlan -Plan $plan
  $caseCount = 1

  $invalidTarget = $plan.PSObject.Copy()
  $invalidTarget.Target = 'unexpected-target'
  try {
    Assert-EnginePreviewBuildPlan -Plan $invalidTarget
    throw 'Invalid target was accepted.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'Unexpected Engine Preview target') {
      throw
    }
  }
  $caseCount++

  $localRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local')).TrimEnd('\', '/')
  $buildRoot = [IO.Path]::GetFullPath($plan.BuildRoot).TrimEnd('\', '/')
  Assert-EnginePreviewBuildRoot -Plan $plan -SyntheticAttributes @{
    $localRoot = [System.IO.FileAttributes]::Directory
    $buildRoot = [System.IO.FileAttributes]::Directory
  }
  $caseCount++

  $outsideRoot = $plan.PSObject.Copy()
  $outsideRoot.BuildRoot = Join-Path $repo 'build'
  $outsideCaught = $false
  try { Assert-EnginePreviewBuildRoot -Plan $outsideRoot } catch { $outsideCaught = $_.Exception.Message -match 'under the repository .local root' }
  if (-not $outsideCaught) { throw 'Engine Preview build self-test expected an outside-root rejection.' }
  $caseCount++

  $reparseParentCaught = $false
  try {
    Assert-EnginePreviewBuildRoot -Plan $plan -SyntheticAttributes @{
      $localRoot = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint
      $buildRoot = [System.IO.FileAttributes]::Directory
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Engine Preview build self-test expected a reparse-parent rejection.' }
  $caseCount++

  $reparseTargetCaught = $false
  try {
    Assert-EnginePreviewBuildRoot -Plan $plan -SyntheticAttributes @{
      $localRoot = [System.IO.FileAttributes]::Directory
      $buildRoot = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Engine Preview build self-test expected a reparse-target rejection.' }
  $caseCount++

  $nonDirectoryCaught = $false
  try {
    Assert-EnginePreviewBuildRoot -Plan $plan -SyntheticAttributes @{
      $localRoot = [System.IO.FileAttributes]::Directory
      $buildRoot = [System.IO.FileAttributes]::Archive
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Engine Preview build self-test expected a non-directory rejection.' }
  $caseCount++

  $leafInspectionErrorCaught = $false
  try {
    Assert-EnginePreviewBuildRoot -Plan $plan `
      -SyntheticAttributes @{ $localRoot = [System.IO.FileAttributes]::Directory } `
      -SyntheticInspectionErrors @{ $buildRoot = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'Engine Preview build self-test expected a leaf inspection-error rejection.' }
  $caseCount++

  $parentInspectionErrorCaught = $false
  try {
    Assert-EnginePreviewBuildRoot -Plan $plan `
      -SyntheticAttributes @{ $buildRoot = [System.IO.FileAttributes]::Directory } `
      -SyntheticInspectionErrors @{ $localRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'Engine Preview build self-test expected a parent inspection-error rejection.' }
  $caseCount++

  Write-Output "Engine Preview build self-test passed ($caseCount cases; offline/no-build/no-process/no-file-write)."
}

if ($SelfTest) {
  Invoke-EnginePreviewBuildSelfTest
  exit 0
}

$plan = Get-EnginePreviewBuildPlan -RepositoryRoot $repo
Assert-EnginePreviewBuildRoot -Plan $plan

cmake @($plan.ConfigureArguments)
if ($LASTEXITCODE -ne 0) { throw "Engine preview configure failed: $LASTEXITCODE" }
Assert-EnginePreviewBuildRoot -Plan $plan
cmake @($plan.BuildArguments)
if ($LASTEXITCODE -ne 0) { throw "Engine preview build failed: $LASTEXITCODE" }
Write-Output "Engine Preview build succeeded. Start the executable from $($plan.BuildRoot) before connecting DesktopCompat."
