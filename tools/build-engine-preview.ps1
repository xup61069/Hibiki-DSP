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

  Write-Output 'Engine Preview build self-test passed (5 cases; offline/no-build/no-process/no-file-write).'
}

if ($SelfTest) {
  Invoke-EnginePreviewBuildSelfTest
  exit 0
}

$plan = Get-EnginePreviewBuildPlan -RepositoryRoot $repo

cmake @($plan.ConfigureArguments)
if ($LASTEXITCODE -ne 0) { throw "Engine preview configure failed: $LASTEXITCODE" }
cmake @($plan.BuildArguments)
if ($LASTEXITCODE -ne 0) { throw "Engine preview build failed: $LASTEXITCODE" }
Write-Output "Engine Preview build succeeded. Start the executable from $($plan.BuildRoot) before connecting DesktopCompat."
