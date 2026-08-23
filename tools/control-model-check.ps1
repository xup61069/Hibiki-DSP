#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'apps/control-model-check/Hibiki.ControlModel.Check.csproj'
$outputRoot = Join-Path $repo '.local/dotnet/'
$objRoot = Join-Path $outputRoot 'obj/'

function Test-ControlModelCheckPathUnderRoot {
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

function Get-ControlModelCheckExistingAttributes {
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
    throw "Control model check path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Control model check path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-ControlModelCheckPath {
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
  if (-not (Test-ControlModelCheckPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Control model check path must remain under the repository root: $fullPath"
  }

  $leafAttributes = Get-ControlModelCheckExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Control model check $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-ControlModelCheckExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Control model check path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Control model check path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Control model check path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Control model check path parent is not a directory: $cursor"
      }
    }
    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Control model check path could not reach the repository root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-ControlModelCheck {
  param(
    [Parameter(Mandatory = $true)][string]$ProjectPath,
    [Parameter(Mandatory = $true)][string]$BaseOutputPath,
    [Parameter(Mandatory = $true)][string]$ProjectExtensionsPath,
    [scriptblock]$CommandRunner,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Control model check project was not found: $ProjectPath"
  }
  if ([string]::IsNullOrWhiteSpace($BaseOutputPath) -or
      [string]::IsNullOrWhiteSpace($ProjectExtensionsPath)) {
    throw 'Control model check output paths must be non-empty.'
  }

  Assert-ControlModelCheckPath -Path $ProjectPath -Root $repo -Kind File -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  Assert-ControlModelCheckPath -Path $BaseOutputPath -Root $repo -Kind Directory -AllowMissingLeaf -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  Assert-ControlModelCheckPath -Path $ProjectExtensionsPath -Root $repo -Kind Directory -AllowMissingLeaf -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors

  if ($null -eq $CommandRunner) {
    dotnet run --project $ProjectPath --configuration Release --nologo `
      "-p:BaseOutputPath=$BaseOutputPath" "-p:MSBuildProjectExtensionsPath=$ProjectExtensionsPath"
    $exitCode = $LASTEXITCODE
  }
  else {
    $runnerResult = @(@(& $CommandRunner $ProjectPath $BaseOutputPath $ProjectExtensionsPath))
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

  Assert-SelfTestRejection -Label 'outside-output-path' -ExpectedPattern 'must remain under the repository root' -Action {
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath 'C:/hibiki-outside-control-model-check' `
      -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'reparse-output-parent' -ExpectedPattern 'reparse point' -Action {
    $reparseKey = [IO.Path]::GetFullPath((Join-Path $repo '.local/dotnet')).TrimEnd('\', '/')
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot `
      -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner `
      -SyntheticAttributes @{ $reparseKey = [System.IO.FileAttributes]([System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint) }
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'wrong-kind-project' -ExpectedPattern 'is not a file' -Action {
    $projectKey = [IO.Path]::GetFullPath($project).TrimEnd('\', '/')
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot `
      -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner `
      -SyntheticAttributes @{ $projectKey = [System.IO.FileAttributes]::Directory }
  }
  $caseCount++

  Assert-SelfTestRejection -Label 'inspection-failure' -ExpectedPattern 'path inspection failed' -Action {
    $objKey = [IO.Path]::GetFullPath($objRoot).TrimEnd('\', '/')
    Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot `
      -ProjectExtensionsPath $objRoot -CommandRunner $captureRunner `
      -SyntheticInspectionErrors @{ $objKey = 'synthetic access denied' }
  }
  $caseCount++

  Write-Output "Control model check self-test passed ($caseCount cases)."
  exit 0
}

Invoke-ControlModelCheck -ProjectPath $project -BaseOutputPath $outputRoot -ProjectExtensionsPath $objRoot
