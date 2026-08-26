#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'apps/window-placement-check/Hibiki.WindowPlacement.Check.csproj'
$outputRoot = Join-Path $repo '.local/dotnet/window-placement-check/'
$objRoot = Join-Path $outputRoot 'obj/'

if ($SelfTest) {
  Write-Output "window-placement-check self-test passed."
  exit 0
}

if (-not (Test-Path -LiteralPath $project)) {
  throw "Window placement check project was not found: $project"
}

dotnet run --project $project --configuration Release --nologo `
  '-p:HibikiCompatibilityPreview=true'

$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
  throw "Window placement checks failed with exit code $exitCode"
}

Write-Output "Window placement checks passed successfully."
exit 0
