#Requires -Version 7
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'apps/control-model-scene-sync-check/Hibiki.ControlModel.SceneSync.Check.csproj'
$outputRoot = Join-Path $repo '.local/dotnet/scene-sync-check/'
$objRoot = Join-Path $outputRoot 'obj/'

dotnet run --project $project --configuration Release --nologo `
  "-p:BaseOutputPath=$outputRoot" "-p:MSBuildProjectExtensionsPath=$objRoot"
if ($LASTEXITCODE -ne 0) {
  throw "Connected scene synchronization checks failed with exit code $LASTEXITCODE"
}
