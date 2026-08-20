[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'apps/control-model-check/Hibiki.ControlModel.Check.csproj'
$outputRoot = Join-Path $repo '.local/dotnet/'
$objRoot = Join-Path $outputRoot 'obj/'
dotnet run --project $project --configuration Release --nologo `
  "-p:BaseOutputPath=$outputRoot" "-p:MSBuildProjectExtensionsPath=$objRoot"
if ($LASTEXITCODE -ne 0) { throw "Control model checks failed with exit code $LASTEXITCODE" }
