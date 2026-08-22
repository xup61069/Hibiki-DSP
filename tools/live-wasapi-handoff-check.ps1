[CmdletBinding()]
param(
  [switch]$SelfTest
)

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
New-Item -ItemType Directory -Path $build -Force | Out-Null
& cmake @((Get-LiveWasapiHandoffProperty -Object $plan -Name 'cmake_args'))
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
& cmake --build $build --config RelWithDebInfo --target $LiveWasapiHandoffTarget --parallel
if ($LASTEXITCODE -ne 0) { throw "Probe build failed with exit code $LASTEXITCODE" }
$probe = [string](Get-LiveWasapiHandoffProperty -Object $plan -Name 'probe_path')
if (-not (Test-Path -LiteralPath $probe)) { throw "Probe executable missing: $probe" }
& $probe
if ($LASTEXITCODE -ne 0) { throw "Live WASAPI handoff probe failed with exit code $LASTEXITCODE" }
