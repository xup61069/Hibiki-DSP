[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'config/distribution-profile.yml'
$text = Get-Content -LiteralPath $path -Raw
foreach ($required in @('hibiki-public-2026', 'Root\\HibikiDSP', 'HibikiVirtualAudio', 'Local\\HibikiDSP\\v1', 'HibikiDSP_v1_control', 'Local\\HibikiDSP_v1_asio')) {
  if (-not $text.Contains($required)) { throw "Distribution profile missing stable value: $required" }
}
$guidNames = @('endpoint_main_guid', 'endpoint_low_latency_guid', 'endpoint_surround_guid', 'endpoint_virtual_mic_guid',
  'volume_ui_context_guid', 'volume_safety_context_guid', 'volume_scene_context_guid', 'volume_session_context_guid',
  'asio_clsid')
$values = @()
foreach ($name in $guidNames) {
  $match = [regex]::Match($text, "(?m)^\s*${name}:\s*([0-9a-fA-F-]{36})\s*$")
  if (-not $match.Success) { throw "Invalid or missing GUID: $name" }
  $values += $match.Groups[1].Value.ToLowerInvariant()
}
if (($values | Sort-Object -Unique).Count -ne $values.Count) { throw 'Distribution GUIDs must be unique.' }
if ($text -notmatch '(?m)^\s*schema_version:\s*1\s*$') { throw 'Distribution schema version must remain 1.' }
Write-Output 'Distribution profile checks passed.'
