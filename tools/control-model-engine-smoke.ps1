[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
$project = Join-Path $repo 'apps/control-model-engine-smoke/Hibiki.ControlModel.EngineSmoke.csproj'
$outputRoot = Join-Path $repo '.local/control-model-engine-smoke'

if (-not (Test-Path -LiteralPath $engine)) {
  & (Join-Path $repo 'tools/build-engine-preview.ps1')
  if ($LASTEXITCODE -ne 0) { throw "Engine Preview build failed: $LASTEXITCODE" }
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this smoke.'
}

$engineProcess = Start-Process -FilePath $engine -WorkingDirectory (Split-Path $engine) `
  -WindowStyle Hidden -PassThru
try {
  dotnet run --project $project --configuration Release --nologo `
    "-p:BaseOutputPath=$outputRoot/bin/" `
    "-p:MSBuildProjectExtensionsPath=$outputRoot/obj/"
  if ($LASTEXITCODE -ne 0) { throw "Control model Engine Preview smoke failed: $LASTEXITCODE" }
}
finally {
  if (-not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id -ErrorAction SilentlyContinue
    $engineProcess.WaitForExit()
  }
}

Write-Output 'Control model Engine Preview smoke completed.'
