[CmdletBinding()]
param(
  [switch]$EnableSessionRouting,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
$project = Join-Path $repo 'apps/control-model-engine-smoke/Hibiki.ControlModel.EngineSmoke.csproj'
$outputRoot = Join-Path $repo '.local/control-model-engine-smoke'

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

  $cases = 4
  Write-Output "Control model Engine Preview smoke launcher self-test passed ($cases cases)."
  exit 0
}

if (-not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this smoke.'
}

$engineArguments = @()
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments -WorkingDirectory (Split-Path $engine) `
  -WindowStyle Hidden -PassThru
try {
  $smokeArguments = @(Get-ControlModelSmokeArguments -Project $project -OutputRoot $outputRoot -EnableSessionRouting:$EnableSessionRouting)
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
