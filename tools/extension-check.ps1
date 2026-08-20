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
foreach ($path in @('extensions/service-worker.js', 'extensions/popup.html', 'extensions/popup.js', 'extensions/offscreen.html', 'extensions/offscreen.js', 'extensions/audio-worklet.js')) {
  if (-not (Test-Path (Join-Path $repo $path))) { throw "Missing extension source: $path" }
}
if ($manifest.host_permissions -notcontains 'http://127.0.0.1/*') {
  throw 'Native bridge host permission is missing.'
}
if (-not [string]$manifest.content_security_policy.extension_pages -match 'ws://127\.0\.0\.1:17842') {
  throw 'Native bridge WebSocket CSP is missing.'
}
Write-Output 'Browser extension source checks passed.'
