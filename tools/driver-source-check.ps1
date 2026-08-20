[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$adapter = Join-Path $repo 'driver/wdk/hibiki_property_adapter.cpp'
$readme = Join-Path $repo 'driver/wdk/README.md'
foreach ($path in @($adapter, $readme)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing WDK source boundary: $path" }
}
$source = Get-Content -LiteralPath $adapter -Raw
if ($source -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'WDK adapter must retain MS-PL.' }
if ($source -match 'audio_engine|scene_graph|asio_bridge|plugin_host') {
    throw 'WDK adapter must not link GPL user-space implementation.'
}
if ($source -notmatch 'KSPROPERTY_AUDIO_VOLUMELEVEL' -or $source -notmatch 'KSPROPERTY_AUDIO_MUTE') {
    throw 'WDK adapter volume/mute dispatch is incomplete.'
}
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
