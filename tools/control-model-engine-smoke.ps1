[CmdletBinding()]
param(
  [switch]$EnableSessionRouting,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-ControlModelSmokePathPlan {
  param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
  )

  $localRoot = Join-Path $RepositoryRoot '.local'
  $engineWorkingDirectory = Join-Path $localRoot 'engine-preview/Release'
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    LocalRoot = $localRoot
    EngineWorkingDirectory = $engineWorkingDirectory
    EnginePath = Join-Path $engineWorkingDirectory 'hibiki_engine_preview.exe'
    ProjectPath = Join-Path $RepositoryRoot 'apps/control-model-engine-smoke/Hibiki.ControlModel.EngineSmoke.csproj'
    OutputRoot = Join-Path $localRoot 'control-model-engine-smoke'
  }
}

function Test-ControlModelSmokePathUnderRoot {
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

function Get-ControlModelSmokeExistingAttributes {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [hashtable]$SyntheticAttributes
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if (-not (Test-Path -LiteralPath $fullPath)) { return $null }
  return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force).Attributes
}

function Assert-ControlModelSmokePath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-ControlModelSmokePathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Control-model smoke path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-ControlModelSmokeExistingAttributes -Path $fullPath -SyntheticAttributes $SyntheticAttributes
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Control-model smoke $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-ControlModelSmokeExistingAttributes -Path $cursor -SyntheticAttributes $SyntheticAttributes
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Control-model smoke path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Control-model smoke path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Control-model smoke path is not a file: $fullPath"
        }
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Control-model smoke path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Get-ControlModelSmokeArguments {
  param(
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [switch]$EnableSessionRouting
  )

  $arguments = @(
    '--project', $Project, '--configuration', 'Release', '--nologo',
    "-p:BaseOutputPath=$OutputRoot/bin/",
    "-p:MSBuildProjectExtensionsPath=$OutputRoot/obj/"
  )
  if ($EnableSessionRouting) { $arguments += '--session' }
  return $arguments
}

function Assert-ControlModelSmokeArguments {
  param(
    [Parameter(Mandatory = $true)][object[]]$Actual,
    [Parameter(Mandatory = $true)][object[]]$Expected,
    [Parameter(Mandatory = $true)][string]$CaseName
  )

  if (($Actual -join "`n") -ne ($Expected -join "`n")) {
    throw "Control model smoke argument mismatch in ${CaseName}: actual=[$($Actual -join ', ')], expected=[$($Expected -join ', ')]"
  }
}

function Invoke-ControlModelSmokePathSelfTest {
  $fixture = Get-ControlModelSmokePathPlan -RepositoryRoot 'C:\hibiki-control-model-smoke-selftest'
  $repositoryRoot = [IO.Path]::GetFullPath($fixture.RepositoryRoot).TrimEnd('\', '/')
  $localRoot = [IO.Path]::GetFullPath($fixture.LocalRoot).TrimEnd('\', '/')
  $workingDirectory = [IO.Path]::GetFullPath($fixture.EngineWorkingDirectory).TrimEnd('\', '/')
  $enginePath = [IO.Path]::GetFullPath($fixture.EnginePath).TrimEnd('\', '/')
  $projectPath = [IO.Path]::GetFullPath($fixture.ProjectPath).TrimEnd('\', '/')
  $outputRoot = [IO.Path]::GetFullPath($fixture.OutputRoot).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-ControlModelSmokePath -Path $fixture.ProjectPath -Root $fixture.RepositoryRoot -Kind File -SyntheticAttributes @{
    $repositoryRoot = $directory
    $projectPath = $file
  }
  $cases++
  Assert-ControlModelSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
    $localRoot = $directory
    $workingDirectory = $directory
    $enginePath = $file
  }
  $cases++
  Assert-ControlModelSmokePath -Path $fixture.OutputRoot -Root $fixture.LocalRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{
    $localRoot = $directory
  }
  $cases++

  $existingOutput = @{
    $localRoot = $directory
    $outputRoot = $directory
  }
  Assert-ControlModelSmokePath -Path $fixture.OutputRoot -Root $fixture.LocalRoot -Kind Directory -SyntheticAttributes $existingOutput
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path $fixture.RepositoryRoot 'outside-output'
  try { Assert-ControlModelSmokePath -Path $outsidePath -Root $fixture.LocalRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Control-model smoke self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $workingDirectory = $directory
      $enginePath = $file
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Control-model smoke self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.ProjectPath -Root $fixture.RepositoryRoot -Kind File -SyntheticAttributes @{
      $repositoryRoot = $directory
      $projectPath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Control-model smoke self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.EngineWorkingDirectory -Root $fixture.LocalRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Control-model smoke self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.ProjectPath -Root $fixture.RepositoryRoot -Kind File -SyntheticAttributes @{
      $repositoryRoot = $directory
      $projectPath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Control-model smoke self-test expected a non-file rejection.' }
  $cases++

  $missingEngineCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $directory
    }
  } catch { $missingEngineCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingEngineCaught) { throw 'Control-model smoke self-test expected a missing executable rejection.' }
  $cases++

  $outputTypeCaught = $false
  try {
    Assert-ControlModelSmokePath -Path $fixture.OutputRoot -Root $fixture.LocalRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $outputRoot = $file
    }
  } catch { $outputTypeCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $outputTypeCaught) { throw 'Control-model smoke self-test expected an output-root type rejection.' }
  $cases++

  return $cases
}

if ($SelfTest) {
  $projectFixture = 'selftest/project/Hibiki.ControlModel.EngineSmoke.csproj'
  $outputFixture = 'selftest/output'

  $default = @(Get-ControlModelSmokeArguments -Project $projectFixture -OutputRoot $outputFixture)
  Assert-ControlModelSmokeArguments -Actual $default -Expected @(
    '--project', $projectFixture, '--configuration', 'Release', '--nologo',
    "-p:BaseOutputPath=$outputFixture/bin/",
    "-p:MSBuildProjectExtensionsPath=$outputFixture/obj/"
  ) -CaseName 'default'
  if ($default -contains '--session') { throw 'Default smoke arguments must not include --session.' }

  $session = @(Get-ControlModelSmokeArguments -Project $projectFixture -OutputRoot $outputFixture -EnableSessionRouting)
  Assert-ControlModelSmokeArguments -Actual $session -Expected @($default + '--session') -CaseName 'session-routing'
  if ($session[-1] -ne '--session') { throw 'Session-routing smoke argument must be the final token.' }

  $alternateOutput = 'selftest/alternate-output'
  $alternate = @(Get-ControlModelSmokeArguments -Project $projectFixture -OutputRoot $alternateOutput)
  if ($alternate[1] -ne $projectFixture -or
      $alternate[5] -eq $default[5] -or
      $alternate[6] -eq $default[6]) {
    throw 'Smoke argument self-test expected output-root isolation without project drift.'
  }

  $cases = 4 + (Invoke-ControlModelSmokePathSelfTest)
  Write-Output "Control model Engine Preview smoke launcher path and argument self-test passed ($cases cases; offline/no-process/no-dotnet/no-file-write)."
  exit 0
}

$smokePlan = Get-ControlModelSmokePathPlan -RepositoryRoot $repo
Assert-ControlModelSmokePath -Path $smokePlan.ProjectPath -Root $smokePlan.RepositoryRoot -Kind File
Assert-ControlModelSmokePath -Path $smokePlan.OutputRoot -Root $smokePlan.LocalRoot -Kind Directory -AllowMissingLeaf
Assert-ControlModelSmokePath -Path $smokePlan.EnginePath -Root $smokePlan.LocalRoot -Kind File -AllowMissingLeaf
Assert-ControlModelSmokePath -Path $smokePlan.EngineWorkingDirectory -Root $smokePlan.LocalRoot -Kind Directory -AllowMissingLeaf
$engine = $smokePlan.EnginePath
if (-not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
Assert-ControlModelSmokePath -Path $smokePlan.EnginePath -Root $smokePlan.LocalRoot -Kind File
Assert-ControlModelSmokePath -Path $smokePlan.EngineWorkingDirectory -Root $smokePlan.LocalRoot -Kind Directory
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this smoke.'
}

$engineArguments = @()
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments -WorkingDirectory $smokePlan.EngineWorkingDirectory `
  -WindowStyle Hidden -PassThru
try {
  Assert-ControlModelSmokePath -Path $smokePlan.ProjectPath -Root $smokePlan.RepositoryRoot -Kind File
  Assert-ControlModelSmokePath -Path $smokePlan.OutputRoot -Root $smokePlan.LocalRoot -Kind Directory -AllowMissingLeaf
  $smokeArguments = @(Get-ControlModelSmokeArguments -Project $smokePlan.ProjectPath -OutputRoot $smokePlan.OutputRoot -EnableSessionRouting:$EnableSessionRouting)
  dotnet run @smokeArguments
  if ($LASTEXITCODE -ne 0) { throw "Control model Engine Preview smoke failed: $LASTEXITCODE" }
}
finally {
  if (-not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
    $engineProcess.WaitForExit()
  }
}

Write-Output 'Control model Engine Preview smoke completed.'
