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
$propertyAdapter = Join-Path $repo 'driver/wdk/hibiki_property_adapter.cpp'
$miniportHeader = Join-Path $repo 'driver/wdk/hibiki_miniport_wavert.h'
$miniportSource = Join-Path $repo 'driver/wdk/hibiki_miniport_wavert.cpp'
$filterHeader = Join-Path $repo 'driver/wdk/hibiki_filter_tables.h'
$filterSource = Join-Path $repo 'driver/wdk/hibiki_filter_tables.cpp'
$readme = Join-Path $repo 'driver/wdk/README.md'
$inf = Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf'
$infReadme = Join-Path $repo 'driver/inf/README.md'
foreach ($path in @($adapter, $topologyHeader, $topologySource, $streamHeader, $streamSource, $streamAdapter, $propertyAdapter, $miniportHeader, $miniportSource, $filterHeader, $filterSource, $readme, $inf, $infReadme)) {
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
if ($source -notmatch 'KSPROPERTY_TYPE_BASICSUPPORT' -or
    $source -notmatch 'KSPROPERTY_TYPE_GET\s*\|\s*KSPROPERTY_TYPE_SET' -or
    $source -notmatch 'STATUS_BUFFER_TOO_SMALL') {
    throw 'WDK adapter must expose GET/SET basic support and bounded buffer negotiation.'
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
    'HibikiWaveRtPinReadRenderV1', 'HibikiWaveRtPinInitializeEndpointV1',
    'HibikiWaveRtPinInitializeCaptureEndpointV1', 'HibikiWaveRtBuildFormatEndpointV1',
    'HibikiWaveRtBuildFormatV1', 'KSDATAFORMAT_SUBTYPE_IEEE_FLOAT',
    'hibiki_wavert_stream_pop_or_silence_v1')) {
    if (-not $adapterSource.Contains($required)) { throw "WDK stream adapter missing required boundary: $required" }
}
if ($adapterSource -match '(?i)malloc|calloc|realloc|free|CreateThread|audio_engine|scene_graph|asio_bridge') {
    throw 'WDK stream adapter must remain non-allocating and independent from GPL user-space.'
}
$propertySource = Get-Content -LiteralPath $propertyAdapter -Raw
foreach ($required in @('HibikiPropertyContextInitializeEndpointV1',
    'hibiki_endpoint_topology_get_v1', 'hibiki_wavert_endpoint_state_init_v1')) {
    if (-not $propertySource.Contains($required)) { throw "WDK property adapter missing endpoint initialization: $required" }
}
if ($propertySource -match '(?i)audio_engine|scene_graph|asio_bridge|malloc|calloc|realloc') {
    throw 'WDK property adapter must remain independent from GPL user-space and allocation.'
}
$miniportSrc = (Get-Content -LiteralPath $miniportHeader -Raw) + (Get-Content -LiteralPath $miniportSource -Raw)
if ($miniportSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls miniport adapter must retain MS-PL.' }
if ($miniportSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') {
    throw 'PortCls miniport adapter must not link GPL user-space implementation.'
}
foreach ($required in @('IMiniportWaveRT', 'IMiniportWaveRTStreamNotification',
    'HibikiMiniportWaveRtV1', 'HibikiMiniportWaveRtStreamV1',
    'AllocateAudioBufferWithNotification', 'FreeAudioBufferWithNotification',
    'RegisterNotificationEvent', 'UnregisterNotificationEvent',
    'GetHWLatency', 'GetPosition', 'SetState', 'NewStream', 'InitEndpoint')) {
    if (-not $miniportSrc.Contains($required)) { throw "PortCls miniport missing required method: $required" }
}
if ($miniportSrc -match '(?i)CreateThread|KeWaitFor') {
    throw 'PortCls miniport must remain non-blocking.'
}
$filterSrc = (Get-Content -LiteralPath $filterHeader -Raw) + (Get-Content -LiteralPath $filterSource -Raw)
if ($filterSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls filter tables must retain MS-PL.' }
if ($filterSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') {
    throw 'PortCls filter tables must not link GPL user-space implementation.'
}
foreach ($required in @('HibikiGetFilterDescriptorEndpointV1', 'HibikiDataRangeIntersectionEndpointV1',
    'PCFILTER_DESCRIPTOR', 'PCPIN_DESCRIPTOR', 'PCNODE_DESCRIPTOR', 'PCCONNECTION_DESCRIPTOR',
    'KSNODETYPE_VOLUME', 'KSNODETYPE_MUTE', 'KSPROPERTY_AUDIO_VOLUMELEVEL', 'KSPROPERTY_AUDIO_MUTE',
    'FilterDescriptor_Main', 'FilterDescriptor_LowLatency', 'FilterDescriptor_Surround', 'FilterDescriptor_VirtualMic')) {
    if (-not $filterSrc.Contains($required)) { throw "PortCls filter tables missing required symbol: $required" }
}
$infSource = Get-Content -LiteralPath $inf -Raw
foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'EndpointMainGuid', 'EndpointVirtualMicGuid', 'PnpLockdown=1')) {
    if (-not $infSource.Contains($required)) { throw "Driver INF source missing required boundary: $required" }
}
if ($infSource -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll') {
    throw 'Driver INF must not contain release credentials or GPL user-space payloads.'
}
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
