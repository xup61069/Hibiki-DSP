[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repo 'extensions/manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.manifest_version -ne 3) { throw 'Extension must use MV3.' }
foreach ($permission in @('activeTab', 'tabCapture', 'offscreen')) {
  if ($manifest.permissions -notcontains $permission) { throw "Missing permission: $permission" }
}
foreach ($path in @('extensions/service-worker.js', 'extensions/popup.html', 'extensions/popup.js', 'extensions/offscreen.html', 'extensions/offscreen.js')) {
  if (-not (Test-Path (Join-Path $repo $path))) { throw "Missing extension source: $path" }
}
Write-Output 'Browser extension source checks passed.'
