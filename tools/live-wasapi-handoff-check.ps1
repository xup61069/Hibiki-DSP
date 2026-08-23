[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

$LiveWasapiHandoffTarget = 'hibiki_live_wasapi_handoff_probe'
$LiveWasapiHandoffBuildRelativePath = '.local/live-wasapi-handoff-build'
$LiveWasapiHandoffProbeRelativePath = 'tests/RelWithDebInfo/hibiki_live_wasapi_handoff_probe.exe'

function Test-LiveWasapiHandoffProperty {
  param(
    [AllowNull()]$Object,
    [Parameter(Mandatory = $true)][string]$Name
  )

  if ($null -eq $Object) {
    return $false
  }

  if ($Object -is [System.Collections.IDictionary]) {
    return $Object.Contains($Name)
  }

  return $null -ne $Object.PSObject.Properties[$Name]
}

function Get-LiveWasapiHandoffProperty {
  param(
    [AllowNull()]$Object,
    [Parameter(Mandatory = $true)][string]$Name
  )

  if (-not (Test-LiveWasapiHandoffProperty -Object $Object -Name $Name)) {
    return $null
  }

  if ($Object -is [System.Collections.IDictionary]) {
    return $Object[$Name]
  }

  return $Object.PSObject.Properties[$Name].Value
}

function New-LiveWasapiHandoffPlan {
  param(
    [Parameter(Mandatory = $true)][string]$RepoRoot
  )

  if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    throw 'Live WASAPI handoff plan requires a repository root.'
  }

  $buildRoot = Join-Path $RepoRoot $LiveWasapiHandoffBuildRelativePath
  return [ordered]@{
    repo_root = $RepoRoot
    build_root = $buildRoot
    cmake_args = @(
      '-S', $RepoRoot,
      '-B', $buildRoot,
      '-DHIBIKI_BUILD_TESTS=ON',
      '-DHIBIKI_BUILD_LIVE_PROBES=ON'
    )
    target = $LiveWasapiHandoffTarget
    probe_path = Join-Path $buildRoot $LiveWasapiHandoffProbeRelativePath
  }
}

function Test-LiveWasapiHandoffPathUnderRoot {
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

function Get-LiveWasapiHandoffExistingAttributes {
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
    throw "Live WASAPI handoff path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Live WASAPI handoff path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-LiveWasapiHandoffPath {
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
  if (-not (Test-LiveWasapiHandoffPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Live WASAPI handoff path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-LiveWasapiHandoffExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Live WASAPI handoff $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-LiveWasapiHandoffExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Live WASAPI handoff path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Live WASAPI handoff path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Live WASAPI handoff path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Live WASAPI handoff path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Live WASAPI handoff path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-LiveWasapiHandoffPathSelfTest {
  $repositoryRoot = [IO.Path]::GetFullPath('C:\hibiki-live-wasapi-handoff-selftest').TrimEnd('\', '/')
  $fixture = New-LiveWasapiHandoffPlan -RepoRoot $repositoryRoot
  $localRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.local')).TrimEnd('\', '/')
  $buildRoot = [IO.Path]::GetFullPath([string]$fixture.build_root).TrimEnd('\', '/')
  $probePath = [IO.Path]::GetFullPath([string]$fixture.probe_path).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-LiveWasapiHandoffPath -Path $fixture.build_root -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-LiveWasapiHandoffPath -Path $fixture.build_root -Root $localRoot -Kind Directory -SyntheticAttributes @{
    $localRoot = $directory
    $buildRoot = $directory
  }
  $cases++
  Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -SyntheticAttributes @{
    $buildRoot = $directory
    $probePath = $file
  }
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path $repositoryRoot 'outside-live-wasapi-handoff'
  try { Assert-LiveWasapiHandoffPath -Path $outsidePath -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Live WASAPI handoff self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.build_root -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $buildRoot = $directory
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Live WASAPI handoff self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Live WASAPI handoff self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.build_root -Root $localRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $buildRoot = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Live WASAPI handoff self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
      $probePath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Live WASAPI handoff self-test expected a non-file rejection.' }
  $cases++

  $nonDirectoryParentCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -SyntheticAttributes @{
      $buildRoot = $file
      $probePath = $file
    }
  } catch { $nonDirectoryParentCaught = $_.Exception.Message -match 'parent is not a directory' }
  if (-not $nonDirectoryParentCaught) { throw 'Live WASAPI handoff self-test expected a non-directory-parent rejection.' }
  $cases++

  $missingProbeCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -SyntheticAttributes @{
      $buildRoot = $directory
    }
  } catch { $missingProbeCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingProbeCaught) { throw 'Live WASAPI handoff self-test expected a missing-probe rejection.' }
  $cases++

  $inspectionCaught = $false
  try {
    Assert-LiveWasapiHandoffPath -Path $fixture.probe_path -Root $fixture.build_root -Kind File -AllowMissingLeaf -SyntheticInspectionErrors @{
      $probePath = 'synthetic access denied'
    }
  } catch { $inspectionCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $inspectionCaught) { throw 'Live WASAPI handoff self-test expected an inspection-failure rejection.' }
  $cases++

  return $cases
}

function Assert-LiveWasapiHandoffPlan {
  param(
    [Parameter(Mandatory = $true)]$Plan
  )

  foreach ($name in @('repo_root', 'build_root', 'cmake_args', 'target', 'probe_path')) {
    if (-not (Test-LiveWasapiHandoffProperty -Object $Plan -Name $name)) {
      throw "Live WASAPI handoff plan is missing $name."
    }
  }

  $repoRoot = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'repo_root')
  $buildRoot = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'build_root')
  if ([string]::IsNullOrWhiteSpace($repoRoot) -or [string]::IsNullOrWhiteSpace($buildRoot)) {
    throw 'Live WASAPI handoff plan contains an empty repository or build root.'
  }

  $expected = New-LiveWasapiHandoffPlan -RepoRoot $repoRoot
  if ($buildRoot -ine [string]$expected.build_root) {
    throw 'Live WASAPI handoff build root must remain under the repository .local output root.'
  }

  $repoFull = [IO.Path]::GetFullPath($repoRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
  $buildFull = [IO.Path]::GetFullPath($buildRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
  if (-not $buildFull.StartsWith($repoFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Live WASAPI handoff build root escapes the repository root.'
  }

  $actualArguments = @(Get-LiveWasapiHandoffProperty -Object $Plan -Name 'cmake_args')
  $expectedArguments = @($expected.cmake_args)
  if ($actualArguments.Count -ne $expectedArguments.Count) {
    throw 'Live WASAPI handoff CMake definition list has an unexpected length.'
  }

  for ($index = 0; $index -lt $expectedArguments.Count; $index++) {
    if ([string]$actualArguments[$index] -cne [string]$expectedArguments[$index]) {
      throw "Live WASAPI handoff CMake argument mismatch at index $index."
    }
  }

  if ([string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'target') -cne $LiveWasapiHandoffTarget) {
    throw 'Live WASAPI handoff target is not the bounded probe target.'
  }

  $expectedProbePath = [string](Join-Path $buildRoot $LiveWasapiHandoffProbeRelativePath)
  if ([string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'probe_path') -ine $expectedProbePath) {
    throw 'Live WASAPI handoff probe path does not match the target output root.'
  }
}

function Copy-LiveWasapiHandoffPlan {
  param(
    [Parameter(Mandatory = $true)]$Plan
  )

  return [ordered]@{
    repo_root = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'repo_root')
    build_root = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'build_root')
    cmake_args = @((Get-LiveWasapiHandoffProperty -Object $Plan -Name 'cmake_args'))
    target = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'target')
    probe_path = [string](Get-LiveWasapiHandoffProperty -Object $Plan -Name 'probe_path')
  }
}

function Invoke-LiveWasapiHandoffSelfTest {
  $valid = New-LiveWasapiHandoffPlan -RepoRoot 'fixture-root'

  $wrongTarget = Copy-LiveWasapiHandoffPlan -Plan $valid
  $wrongTarget.target = 'wrong_probe_target'

  $missingBuildDefinition = Copy-LiveWasapiHandoffPlan -Plan $valid
  $missingBuildDefinition.cmake_args = @($missingBuildDefinition.cmake_args | Where-Object { $_ -cne '-DHIBIKI_BUILD_TESTS=ON' })

  $unsafeRoot = Copy-LiveWasapiHandoffPlan -Plan $valid
  $unsafeRoot.build_root = 'outside-root'
  $unsafeRoot.cmake_args[3] = 'outside-root'

  $wrongProbePath = Copy-LiveWasapiHandoffPlan -Plan $valid
  $wrongProbePath.probe_path = 'wrong-probe.exe'

  $cases = @(
    [pscustomobject]@{ name = 'valid dispatch plan'; plan = $valid; expected = $true }
    [pscustomobject]@{ name = 'wrong target'; plan = $wrongTarget; expected = $false }
    [pscustomobject]@{ name = 'missing build definition'; plan = $missingBuildDefinition; expected = $false }
    [pscustomobject]@{ name = 'unsafe output root'; plan = $unsafeRoot; expected = $false }
    [pscustomobject]@{ name = 'probe path mismatch'; plan = $wrongProbePath; expected = $false }
  )

  $passed = 0
  foreach ($case in $cases) {
    $actual = $true
    try {
      Assert-LiveWasapiHandoffPlan -Plan $case.plan
    }
    catch {
      $actual = $false
    }

    if ($actual -ne $case.expected) {
      throw "Live WASAPI handoff self-test case failed: $($case.name)."
    }

    $passed++
  }

  Write-Output ("Live WASAPI handoff self-test: {0}/{1} cases passed." -f $passed, $cases.Count)
  $pathCases = Invoke-LiveWasapiHandoffPathSelfTest
  Write-Output "Live WASAPI handoff path self-test: $pathCases cases passed (offline/no-cmake/no-probe/no-file-write)."
}

if ($SelfTest) {
  Invoke-LiveWasapiHandoffSelfTest
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
if (-not $IsWindows) { throw 'Live WASAPI handoff probe is Windows-only.' }
$plan = New-LiveWasapiHandoffPlan -RepoRoot $repo
Assert-LiveWasapiHandoffPlan -Plan $plan
$build = [string](Get-LiveWasapiHandoffProperty -Object $plan -Name 'build_root')
Assert-LiveWasapiHandoffPath -Path $build -Root (Join-Path $repo '.local') -Kind Directory -AllowMissingLeaf
$probe = [string](Get-LiveWasapiHandoffProperty -Object $plan -Name 'probe_path')
Assert-LiveWasapiHandoffPath -Path $probe -Root $build -Kind File -AllowMissingLeaf
New-Item -ItemType Directory -Path $build -Force | Out-Null
& cmake @((Get-LiveWasapiHandoffProperty -Object $plan -Name 'cmake_args'))
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
& cmake --build $build --config RelWithDebInfo --target $LiveWasapiHandoffTarget --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
Assert-LiveWasapiHandoffPath -Path $build -Root (Join-Path $repo '.local') -Kind Directory
Assert-LiveWasapiHandoffPath -Path $probe -Root $build -Kind File
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live WASAPI handoff probe failed with exit code $LASTEXITCODE" }
