[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

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

function Test-LiveAudioSessionPathUnderRoot {
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

function Get-LiveAudioSessionExistingAttributes {
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
    throw "Live audio-session path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Live audio-session path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-LiveAudioSessionPath {
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
  if (-not (Test-LiveAudioSessionPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Live audio-session path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-LiveAudioSessionExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Live audio-session $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-LiveAudioSessionExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Live audio-session path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Live audio-session path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Live audio-session path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Live audio-session path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Live audio-session path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-LiveAudioSessionPathSelfTest {
  $repositoryRoot = [IO.Path]::GetFullPath('C:\hibiki-live-audio-session-selftest').TrimEnd('\', '/')
  $fixture = Get-LiveAudioSessionPlan $repositoryRoot
  $localRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.local')).TrimEnd('\', '/')
  $buildRoot = [IO.Path]::GetFullPath($fixture.BuildRoot).TrimEnd('\', '/')
  $probePath = [IO.Path]::GetFullPath($fixture.ProbePath).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-LiveAudioSessionPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveAudioSessionPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
    $localRoot = $directory
    $buildRoot = $directory
  }
  $cases++
  Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
    $buildRoot = $directory
    $probePath = $file
  }
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path $repositoryRoot 'outside-live-audio-session'
  try { Assert-LiveAudioSessionPath -Path $outsidePath -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Live audio-session self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $buildRoot = $directory
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Live audio-session self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Live audio-session self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.BuildRoot -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $buildRoot = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Live audio-session self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Live audio-session self-test expected a non-file rejection.' }
  $cases++

  $nonDirectoryParentCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $file
      $probePath = $file
    }
  } catch { $nonDirectoryParentCaught = $_.Exception.Message -match 'parent is not a directory' }
  if (-not $nonDirectoryParentCaught) { throw 'Live audio-session self-test expected a non-directory-parent rejection.' }
  $cases++

  $missingProbeCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
    }
  } catch { $missingProbeCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingProbeCaught) { throw 'Live audio-session self-test expected a missing-probe rejection.' }
  $cases++

  $inspectionCaught = $false
  try {
    Assert-LiveAudioSessionPath -Path $fixture.ProbePath -Root $fixture.BuildRoot -Kind File -AllowMissingLeaf -SyntheticInspectionErrors @{
      $probePath = 'synthetic access denied'
    }
  } catch { $inspectionCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $inspectionCaught) { throw 'Live audio-session self-test expected an inspection-failure rejection.' }
  $cases++

  return $cases
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

  $cases = 5 + (Invoke-LiveAudioSessionPathSelfTest)
  Write-Output "Live audio-session wrapper self-test passed ($cases cases; offline/no-cmake/no-probe/no-file-write)."
  exit 0
}

Assert-LiveAudioSessionWindowsOnly $IsWindows

$plan = Get-LiveAudioSessionPlan $repo
Assert-LiveAudioSessionPath -Path $plan.BuildRoot -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf
Assert-LiveAudioSessionPath -Path $plan.ProbePath -Root $plan.BuildRoot -Kind File -AllowMissingLeaf
$build = $plan.BuildRoot
New-Item -ItemType Directory -Path $build -Force | Out-Null
cmake @($plan.ConfigureArgs)
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
cmake --build $build --config RelWithDebInfo --target $plan.Target --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
Assert-LiveAudioSessionPath -Path $plan.BuildRoot -Root (Join-Path $repo '.local') -Kind Directory
$probe = $plan.ProbePath
Assert-LiveAudioSessionPath -Path $probe -Root $plan.BuildRoot -Kind File
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live audio-session probe failed with exit code $LASTEXITCODE" }
