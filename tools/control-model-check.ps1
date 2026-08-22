[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'apps/control-model-check/Hibiki.ControlModel.Check.csproj'
$outputRoot = Join-Path $repo '.local/dotnet/'
$objRoot = Join-Path $outputRoot 'obj/'

function Invoke-ControlModelCheck {
  param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [Parameter(Mandatory = $true)][string]$BaseOutputPath,
    [Parameter(Mandatory = $true)][string]$ProjectExtensionsPath,
    [scriptblock]$CommandRunner
  )

  if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Control model check project was not found: $ProjectPath"
  }
  if ([string]::IsNullOrWhiteSpace($BaseOutputPath) -or
      [string]::IsNullOrWhiteSpace($ProjectExtensionsPath)) {
    throw 'Control model check output paths must be non-empty.'
  }

  if ($null -eq $CommandRunner) {
    dotnet run --project $ProjectPath --configuration Release --nologo `
      "-p:BaseOutputPath=$BaseOutputPath" "-p:MSBuildProjectExtensionsPath=$ProjectExtensionsPath"
    $exitCode = $LASTEXITCODE
  }
  else {
    $runnerResult = @(& $CommandRunner $ProjectPath $BaseOutputPath $ProjectExtensionsPath)
    $exitCode = if ($runnerResult.Count -eq 0) { 0 } else { [int]$runnerResult[-1] }
  }

  if ($exitCode -ne 0) { throw "Control model checks failed with exit code $exitCode" }
}

if ($SelfTest) {
  function Assert-SelfTestRejection {
    param(
      [scriptblock]$Action,
      [string]$ExpectedPattern,
      [string]$Label
    )

    try {
      & $Action
    }
    catch {
      if ("$($_.Exception.Message)" -notmatch $ExpectedPattern) {
        throw "control-model-check self-test case '$Label' returned an unexpected error: $($_.Exception.Message)"
      }
      return
    }
    throw "control-model-check self-test case '$Label' expected a rejection matching '$ExpectedPattern'."
  }

  $caseCount = 0
  $script:ControlModelSelfTestCaptured = $null
  $captureRunner = {
    param($RunProject, $RunBaseOutputPath, $RunProjectExtensionsPath)
    $script:ControlModelSelfTestCaptured = @(
      $RunProject,
      $RunBaseOutputPath,
      $RunProjectExtensionsPath
    )
    return 0
  }

  Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot `
    -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner
  if ($script:ControlModelSelfTestCaptured.Count -ne 3 -or
      $script:ControlModelSelfTestCaptured[0] -ne $project -or
      $script:ControlModelSelfTestCaptured[1] -ne $outputRoot -or
      $script:ControlModelSelfTestCaptured[2] -ne $objRoot) {
    throw 'control-model-check self-test case ''valid-invocation-boundary'' captured unexpected arguments.'
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'missing-project' -ExpectedPattern 'project was not found' -Action {
    Invoke-ControlModelCheck -ProjectPath (Join-Path $repo 'apps/control-model-check/missing.csproj') `
      -BaseOutputPath $outputRoot -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'empty-output-path' -ExpectedPattern 'output paths must be non-empty' -Action {
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath ' ' `
      -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'child-process-failure' -ExpectedPattern 'exit code 23' -Action {
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot `
      -ProjectExtensionsPath $objRoot -CommandRunner { param($RunProject, $RunBaseOutputPath, $RunProjectExtensionsPath) return 23 }
  }
  $caseCount++

  Write-Output "Control model check self-test passed ($caseCount cases)."
  exit 0
}

Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot -ProjectExtensionsPath $objRoot
