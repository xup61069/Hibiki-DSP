[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$adapter = Join-Path $repo 'driver/wdk/hibiki_property_adapter.cpp'
$topologyHeader = Join-Path $repo 'driver/include/hibiki/endpoint_topology_v1.h'
$topologySource = Join-Path $repo 'driver/src/endpoint_topology.c'
$streamHeader = Join-Path $repo 'driver/include/hibiki/wavert_stream_v1.h'
$streamSource = Join-Path $repo 'driver/src/wavert_stream.c'
$streamAdapter = Join-Path $repo 'driver/wdk/hibiki_stream_adapter.cpp'
$readme = Join-Path $repo 'driver/wdk/README.md'
$inf = Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf'
$infReadme = Join-Path $repo 'driver/inf/README.md'
foreach ($path in @($adapter, $topologyHeader, $topologySource, $streamHeader, $streamSource, $streamAdapter, $readme, $inf, $infReadme)) {
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
$topology = (Get-Content -LiteralPath $topologyHeader -Raw) + (Get-Content -LiteralPath $topologySource -Raw)
if ($topology -notmatch 'HIBIKI_CHANNEL_MASK_71_V1' -or
    $topology -notmatch 'HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1' -or
    $topology -notmatch '8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10' -or
    $topology -notmatch 'hibiki_endpoint_topology_validate_v1') {
    throw 'Endpoint topology catalog is missing the fixed channel-mask/GUID contract.'
}
$stream = (Get-Content -LiteralPath $streamHeader -Raw) + (Get-Content -LiteralPath $streamSource -Raw)
foreach ($required in @('HIBIKI_WAVERT_STREAM_ABI_V1', 'hibiki_wavert_stream_push_v1',
    'hibiki_wavert_stream_pop_or_silence_v1', 'dropped_frames', 'underrun_frames')) {
    if (-not $stream.Contains($required)) { throw "WaveRT stream core missing required boundary: $required" }
}
if ($stream -match '(?i)malloc|calloc|realloc|free|CreateThread|KeWaitFor') {
    throw 'WaveRT stream core must remain allocation-free and non-blocking.'
}
$adapterSource = Get-Content -LiteralPath $streamAdapter -Raw
foreach ($required in @('KeAcquireSpinLock', 'KeReleaseSpinLock', 'HibikiWaveRtPinSubmitRenderV1',
    'HibikiWaveRtPinReadRenderV1', 'hibiki_wavert_stream_pop_or_silence_v1')) {
    if (-not $adapterSource.Contains($required)) { throw "WDK stream adapter missing required boundary: $required" }
}
if ($adapterSource -match '(?i)malloc|calloc|realloc|free|CreateThread|audio_engine|scene_graph|asio_bridge') {
    throw 'WDK stream adapter must remain non-allocating and independent from GPL user-space.'
}
$infSource = Get-Content -LiteralPath $inf -Raw
foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'EndpointMainGuid', 'EndpointVirtualMicGuid', 'PnpLockdown=1')) {
    if (-not $infSource.Contains($required)) { throw "Driver INF source missing required boundary: $required" }
}
if ($infSource -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll') {
    throw 'Driver INF must not contain release credentials or GPL user-space payloads.'
}
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
