#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

function Assert-DriverAdapterPolicy {
  param([string]$Text, [string]$SourceName)
  if ($Text -notmatch 'SPDX-License-Identifier: MS-PL') { throw "WDK adapter must retain MS-PL in $SourceName." }
  if ($Text -match 'audio_engine|scene_graph|asio_bridge|plugin_host') { throw "WDK adapter must not link GPL user-space implementation in $SourceName." }
  if ($Text -notmatch 'KSPROPERTY_AUDIO_VOLUMELEVEL' -or $Text -notmatch 'KSPROPERTY_AUDIO_MUTE') { throw "WDK adapter volume/mute dispatch is incomplete in $SourceName." }
  if ($Text -notmatch 'KSPROPERTY_TYPE_BASICSUPPORT' -or $Text -notmatch 'KSPROPERTY_TYPE_GET\s*\|\s*KSPROPERTY_TYPE_SET' -or $Text -notmatch 'STATUS_BUFFER_TOO_SMALL') { throw "WDK adapter must expose GET/SET basic support and bounded buffer negotiation in $SourceName." }
}

function Assert-TopologyPolicy {
  param([string]$Text, [string]$SourceName)
  if ($Text -notmatch 'HIBIKI_CHANNEL_MASK_71_V1' -or $Text -notmatch 'HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1' -or $Text -notmatch '8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10' -or $Text -notmatch 'hibiki_endpoint_topology_validate_v1') { throw "Endpoint topology catalog is missing the fixed channel-mask/GUID contract in $SourceName." }
}

function Assert-StreamCorePolicy {
  param([string]$Text, [string]$SourceName)
  foreach ($required in @('HIBIKI_WAVERT_STREAM_ABI_V1', 'hibiki_wavert_stream_push_v1', 'hibiki_wavert_stream_pop_or_silence_v1', 'dropped_frames', 'underrun_frames')) {
    if (-not $Text.Contains($required)) { throw "WaveRT stream core missing required boundary in ${SourceName}: $required" }
  }
  if ($Text -match '(?i)malloc|calloc|realloc|free|CreateThread|KeWaitFor') { throw "WaveRT stream core must remain allocation-free and non-blocking in $SourceName." }
}

function Assert-StreamAdapterPolicy {
  param([string]$Text, [string]$SourceName)
  foreach ($required in @('KeAcquireSpinLock', 'KeReleaseSpinLock', 'HibikiWaveRtPinSubmitRenderV1', 'HibikiWaveRtPinReadRenderV1', 'HibikiWaveRtBuildFormatV1', 'KSDATAFORMAT_SUBTYPE_IEEE_FLOAT')) {
    if (-not $Text.Contains($required)) { throw "WDK stream adapter missing required boundary in ${SourceName}: $required" }
  }
  if ($Text -match '(?i)malloc|calloc|realloc|free|CreateThread|audio_engine|scene_graph|asio_bridge') { throw "WDK stream adapter must remain non-allocating and independent from GPL user-space in $SourceName." }
}

function Assert-InfPolicy {
  param([string]$Text, [string]$SourceName)
  foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'EndpointMainGuid', 'EndpointVirtualMicGuid', 'PnpLockdown=1')) {
    if (-not $Text.Contains($required)) { throw "Driver INF source missing required boundary in ${SourceName}: $required" }
  }
  if ($Text -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll') { throw "Driver INF must not contain release credentials or GPL user-space payloads in $SourceName." }
}

if ($SelfTest) {
  $caseCount = 0

  $validAdapter = @'
 // SPDX-License-Identifier: MS-PL
 KSPROPERTY_AUDIO_VOLUMELEVEL KSPROPERTY_AUDIO_MUTE
 KSPROPERTY_TYPE_BASICSUPPORT KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET STATUS_BUFFER_TOO_SMALL
'@
  Assert-DriverAdapterPolicy -Text $validAdapter -SourceName 'selftest-valid-adapter'
  $caseCount++

  $missingLicense = $validAdapter.Replace('SPDX-License-Identifier: MS-PL', 'no-license')
  $caught = $false
  try { Assert-DriverAdapterPolicy -Text $missingLicense -SourceName 'selftest-missing-license' } catch { $caught = $true; if ("$($_.Exception.Message)" -notmatch 'MS-PL') { throw } }
  if (-not $caught) { throw 'SelfTest expected missing MS-PL failure.' }
  $caseCount++

  $gplAdapter = $validAdapter + ' audio_engine'
  $caught = $false
  try { Assert-DriverAdapterPolicy -Text $gplAdapter -SourceName 'selftest-gpl' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected GPL linkage failure.' }
  $caseCount++

  $validTopology = 'HIBIKI_CHANNEL_MASK_71_V1 HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1 8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10 hibiki_endpoint_topology_validate_v1'
  Assert-TopologyPolicy -Text $validTopology -SourceName 'selftest-valid-topology'
  $caseCount++

  $invalidTopology = 'HIBIKI_CHANNEL_MASK_71_V1 missing'
  $caught = $false
  try { Assert-TopologyPolicy -Text $invalidTopology -SourceName 'selftest-invalid-topology' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected topology GUID failure.' }
  $caseCount++

  $validStream = 'HIBIKI_WAVERT_STREAM_ABI_V1 hibiki_wavert_stream_push_v1 hibiki_wavert_stream_pop_or_silence_v1 dropped_frames underrun_frames // no alloc'
  Assert-StreamCorePolicy -Text $validStream -SourceName 'selftest-valid-stream'
  $caseCount++

  $allocStream = $validStream + ' malloc('
  $caught = $false
  try { Assert-StreamCorePolicy -Text $allocStream -SourceName 'selftest-alloc-stream' } catch { $caught = $true; if ("$($_.Exception.Message)" -notmatch 'allocation-free') { throw } }
  if (-not $caught) { throw 'SelfTest expected allocation detection failure.' }
  $caseCount++

  $validStreamAdapter = 'KeAcquireSpinLock KeReleaseSpinLock HibikiWaveRtPinSubmitRenderV1 HibikiWaveRtPinReadRenderV1 HibikiWaveRtBuildFormatV1 KSDATAFORMAT_SUBTYPE_IEEE_FLOAT'
  Assert-StreamAdapterPolicy -Text $validStreamAdapter -SourceName 'selftest-valid-stream-adapter'
  $caseCount++

  $missingAdapterSymbol = 'KeAcquireSpinLock missing'
  $caught = $false
  try { Assert-StreamAdapterPolicy -Text $missingAdapterSymbol -SourceName 'selftest-missing-adapter-symbol' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing stream-adapter symbol failure.' }
  $caseCount++

  $validInf = 'Root\HibikiDSP HibikiVirtualAudio.sys EndpointMainGuid EndpointVirtualMicGuid PnpLockdown=1'
  Assert-InfPolicy -Text $validInf -SourceName 'selftest-valid-inf'
  $caseCount++

  $badInf = $validInf + ' GUMROAD'
  $caught = $false
  try { Assert-InfPolicy -Text $badInf -SourceName 'selftest-bad-inf' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected INF credential failure.' }
  $caseCount++

  Write-Output "WDK source boundary gate self-test passed ($caseCount cases)."
  exit 0
}

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
$adapterHeader = Join-Path $repo 'driver/wdk/hibiki_adapter.h'
$adapterMain = Join-Path $repo 'driver/wdk/hibiki_adapter.cpp'
$readme = Join-Path $repo 'driver/wdk/README.md'
$inf = Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf'
$infReadme = Join-Path $repo 'driver/inf/README.md'
foreach ($path in @($adapter, $topologyHeader, $topologySource, $streamHeader, $streamSource, $streamAdapter, $propertyAdapter, $miniportHeader, $miniportSource, $filterHeader, $filterSource, $adapterHeader, $adapterMain, $readme, $inf, $infReadme)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing WDK source boundary: $path" }
}
$source = Get-Content -LiteralPath $adapter -Raw
Assert-DriverAdapterPolicy -Text $source -SourceName 'driver/wdk/hibiki_property_adapter.cpp'
$topology = (Get-Content -LiteralPath $topologyHeader -Raw) + (Get-Content -LiteralPath $topologySource -Raw)
Assert-TopologyPolicy -Text $topology -SourceName 'endpoint_topology'
$stream = (Get-Content -LiteralPath $streamHeader -Raw) + (Get-Content -LiteralPath $streamSource -Raw)
Assert-StreamCorePolicy -Text $stream -SourceName 'wavert_stream'
$adapterSource = Get-Content -LiteralPath $streamAdapter -Raw
Assert-StreamAdapterPolicy -Text $adapterSource -SourceName 'driver/wdk/hibiki_stream_adapter.cpp'
$propertySource = Get-Content -LiteralPath $propertyAdapter -Raw
foreach ($required in @('HibikiPropertyContextInitializeEndpointV1', 'hibiki_endpoint_topology_get_v1', 'hibiki_wavert_endpoint_state_init_v1')) {
    if (-not $propertySource.Contains($required)) { throw "WDK property adapter missing endpoint initialization: $required" }
}
if ($propertySource -match '(?i)audio_engine|scene_graph|asio_bridge|malloc|calloc|realloc') { throw 'WDK property adapter must remain independent from GPL user-space and allocation.' }
$miniportSrc = (Get-Content -LiteralPath $miniportHeader -Raw) + (Get-Content -LiteralPath $miniportSource -Raw)
if ($miniportSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls miniport adapter must retain MS-PL.' }
if ($miniportSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') { throw 'PortCls miniport adapter must not link GPL user-space implementation.' }
foreach ($required in @('IMiniportWaveRT', 'IMiniportWaveRTStreamNotification', 'HibikiMiniportWaveRtV1', 'HibikiMiniportWaveRtStreamV1', 'RegisterNotificationEvent', 'UnregisterNotificationEvent', 'GetHWLatency', 'GetPosition', 'SetState', 'NewStream', 'InitEndpoint')) {
    if (-not $miniportSrc.Contains($required)) { throw "PortCls miniport missing required method: $required" }
}
# Notification-buffer pair: accept either WDK naming generation (Issue #426).
# Legacy kits name them AllocateAudioBufferWithNotification/FreeAudioBufferWithNotification;
# WDK 10.0.26100+ (and Issue #394 ports) name them AllocateBufferWithNotification/FreeBufferWithNotification.
$hasLegacyNotificationPair = $miniportSrc.Contains('AllocateAudioBufferWithNotification') -and
                             $miniportSrc.Contains('FreeAudioBufferWithNotification')
$hasCurrentNotificationPair = $miniportSrc.Contains('AllocateBufferWithNotification') -and
                              $miniportSrc.Contains('FreeBufferWithNotification')
if (-not ($hasLegacyNotificationPair -or $hasCurrentNotificationPair)) {
    throw 'PortCls miniport missing notification-buffer pair (legacy Allocate/FreeAudioBufferWithNotification or current Allocate/FreeBufferWithNotification).'
}
if ($miniportSrc -match '(?i)CreateThread|KeWaitFor') { throw 'PortCls miniport must remain non-blocking.' }
$filterSrc = (Get-Content -LiteralPath $filterHeader -Raw) + (Get-Content -LiteralPath $filterSource -Raw)
if ($filterSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls filter tables must retain MS-PL.' }
if ($filterSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') { throw 'PortCls filter tables must not link GPL user-space implementation.' }
foreach ($required in @('HibikiGetFilterDescriptorEndpointV1', 'HibikiDataRangeIntersectionEndpointV1', 'PCFILTER_DESCRIPTOR', 'PCPIN_DESCRIPTOR', 'PCNODE_DESCRIPTOR', 'PCCONNECTION_DESCRIPTOR', 'KSNODETYPE_VOLUME', 'KSNODETYPE_MUTE', 'KSPROPERTY_AUDIO_VOLUMELEVEL', 'KSPROPERTY_AUDIO_MUTE', 'FilterDescriptor_Main', 'FilterDescriptor_LowLatency', 'FilterDescriptor_Surround', 'FilterDescriptor_VirtualMic')) {
    if (-not $filterSrc.Contains($required)) { throw "PortCls filter tables missing required symbol: $required" }
}
$adapterMainSrc = (Get-Content -LiteralPath $adapterHeader -Raw) + (Get-Content -LiteralPath $adapterMain -Raw)
if ($adapterMainSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls adapter entry must retain MS-PL.' }
if ($adapterMainSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') { throw 'PortCls adapter entry must not link GPL user-space implementation.' }
foreach ($required in @('DriverEntry', 'HibikiAddDevice', 'HibikiStartDevice', 'HibikiRegisterSubdevicesV1', 'PcInitializeAdapterDriver', 'PcAddAdapterDevice', 'PcNewPort', 'PcRegisterSubdevice', 'CLSID_PortWaveRT', 'HIBIKI_SUBDEVICE_NAME_MAIN_V1', 'HIBIKI_SUBDEVICE_NAME_LOW_LATENCY_V1', 'HIBIKI_SUBDEVICE_NAME_SURROUND_V1', 'HIBIKI_SUBDEVICE_NAME_VIRTUAL_MIC_V1')) {
    if (-not $adapterMainSrc.Contains($required)) { throw "PortCls adapter entry missing required symbol: $required" }
}
$infSource = Get-Content -LiteralPath $inf -Raw
Assert-InfPolicy -Text $infSource -SourceName 'driver/inf/HibikiVirtualAudio.inf'
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
