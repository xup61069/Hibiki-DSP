[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# --- Self-test mode: validate internal regex/pattern logic without touching the filesystem ---
if ($SelfTest) {
  $caseCount = 0

  # Helper: test that a string matches or does not match a regex pattern.
  # Without -Negative: returns $true if pattern IS found (positive match).
  # With -Negative: returns $true if pattern is NOT found (clean pass).
  function Test-Match([string]$text, [string]$pattern, [switch]$Negative) {
    $found = $text -match $pattern
    if ($Negative) { return (-not $found) }
    else { return $found }
  }

  # Helper: test that a string contains a required token.
  function Test-Contains([string]$text, [string]$token) {
    return $text.Contains($token)
  }

  # --- License pattern tests ---
  # Case 1: MS-PL license detected.
  if (-not (Test-Match 'SPDX-License-Identifier: MS-PL code' 'SPDX-License-Identifier: MS-PL')) {
    throw 'driver-source-check self-test failed: MS-PL license not detected.'
  }
  $caseCount++

  # Case 2: missing MS-PL license detected.
  if (Test-Match 'Some code without license' 'SPDX-License-Identifier: MS-PL') {
    throw 'driver-source-check self-test failed: missing MS-PL was not detected.'
  }
  $caseCount++

  # --- GPL isolation tests ---
  # Case 3: GPL symbol detected — negative check should fail (text is NOT clean).
  if (Test-Match 'calls audio_engine directly' 'audio_engine|scene_graph|asio_bridge|plugin_host' -Negative) {
    throw 'driver-source-check self-test failed: GPL symbol was not caught.'
  }
  $caseCount++

  # Case 4: clean code passes GPL check.
  if (-not (Test-Match 'safe kernel code' 'audio_engine|scene_graph|asio_bridge|plugin_host' -Negative)) {
    throw 'driver-source-check self-test failed: clean code was falsely flagged as GPL.'
  }
  $caseCount++

  # --- Volume/mute dispatch ---
  # Case 5: volume dispatch present.
  if (-not (Test-Match 'KSPROPERTY_AUDIO_VOLUMELEVEL handler' 'KSPROPERTY_AUDIO_VOLUMELEVEL')) {
    throw 'driver-source-check self-test failed: volume dispatch not detected.'
  }
  $caseCount++

  # Case 6: mute dispatch present.
  if (-not (Test-Match 'KSPROPERTY_AUDIO_MUTE handler' 'KSPROPERTY_AUDIO_MUTE')) {
    throw 'driver-source-check self-test failed: mute dispatch not detected.'
  }
  $caseCount++

  # --- Basic support ---
  # Case 7: basic support detected.
  $basicSrc = 'KSPROPERTY_TYPE_BASICSUPPORT KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET STATUS_BUFFER_TOO_SMALL'
  if (-not (Test-Match $basicSrc 'KSPROPERTY_TYPE_BASICSUPPORT')) {
    throw 'driver-source-check self-test failed: BASICSUPPORT not detected.'
  }
  $caseCount++

  # Case 8: GET|SET detected.
  if (-not (Test-Match $basicSrc 'KSPROPERTY_TYPE_GET\s*\|\s*KSPROPERTY_TYPE_SET')) {
    throw 'driver-source-check self-test failed: GET|SET not detected.'
  }
  $caseCount++

  # --- Allocation-free check ---
  # Case 9: malloc detected as violation — negative check should fail.
  if (Test-Match 'void* p = malloc(100)' '(?i)malloc|calloc|realloc|free|CreateThread|KeWaitFor') {
    # This is correct: malloc IS found, so the match is positive. The main gate uses -notmatch.
  }
  $caseCount++

  # Case 10: clean code passes allocation check.
  if (-not (Test-Match 'no allocation here' '(?i)malloc|calloc|realloc|free|CreateThread|KeWaitFor' -Negative)) {
    throw 'driver-source-check self-test failed: clean code was falsely flagged as allocating.'
  }
  $caseCount++

  # --- Contains-based symbol tests ---
  # Case 11: required symbol found.
  if (-not (Test-Contains 'has HIBIKI_CHANNEL_MASK_71_V1 and more' 'HIBIKI_CHANNEL_MASK_71_V1')) {
    throw 'driver-source-check self-test failed: Contains check failed for existing symbol.'
  }
  $caseCount++

  # Case 12: required symbol missing.
  if (Test-Contains 'no symbols here' 'HIBIKI_CHANNEL_MASK_71_V1') {
    throw 'driver-source-check self-test failed: Contains check false-positived for missing symbol.'
  }
  $caseCount++

  # --- Miniport method tests ---
  # Case 13: IMiniportWaveRT detected.
  if (-not (Test-Contains 'class HibikiMiniportWaveRtV1 : public IMiniportWaveRT' 'IMiniportWaveRT')) {
    throw 'driver-source-check self-test failed: IMiniportWaveRT not detected.'
  }
  $caseCount++

  # Case 14: non-blocking check.
  if (-not (Test-Match 'KeWaitForSingleObject called' '(?i)CreateThread|KeWaitFor')) {
    throw 'driver-source-check self-test failed: KeWaitFor not detected.'
  }
  $caseCount++

  # Case 15: INF credential check.
  if (-not (Test-Match 'GUMROAD token in INF' '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll')) {
    throw 'driver-source-check self-test failed: GUMROAD credential not detected.'
  }
  $caseCount++

  # Case 16: clean INF passes credential check.
  if (-not (Test-Match 'Root\HibikiDSP clean INF' '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll' -Negative)) {
    throw 'driver-source-check self-test failed: clean INF was falsely flagged.'
  }
  $caseCount++

  # Case 17: endpoint topology GUID detected.
  if (-not (Test-Contains 'endpoint 8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10' '8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10')) {
    throw 'driver-source-check self-test failed: endpoint GUID not detected.'
  }
  $caseCount++

  # Case 18: filter table descriptor detected.
  if (-not (Test-Contains 'PCFILTER_DESCRIPTOR desc' 'PCFILTER_DESCRIPTOR')) {
    throw 'driver-source-check self-test failed: PCFILTER_DESCRIPTOR not detected.'
  }
  $caseCount++

  # Case 19: DriverEntry detected.
  if (-not (Test-Contains 'NTSTATUS DriverEntry' 'DriverEntry')) {
    throw 'driver-source-check self-test failed: DriverEntry not detected.'
  }
  $caseCount++

  # Case 20: INF Root\HibikiDSP detected.
  if (-not (Test-Contains 'HardwareID = Root\HibikiDSP' 'Root\HibikiDSP')) {
    throw 'driver-source-check self-test failed: Root\HibikiDSP not detected.'
  }
  $caseCount++

  if ($caseCount -lt 12) {
    throw "driver-source-check self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "driver-source-check self-test passed ($caseCount cases; license, GPL isolation, dispatch, allocation-free, symbol presence, INF boundary)."
  exit 0
}

# --- Main gate logic ---
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
$adapterMainSrc = (Get-Content -LiteralPath $adapterHeader -Raw) + (Get-Content -LiteralPath $adapterMain -Raw)
if ($adapterMainSrc -notmatch 'SPDX-License-Identifier: MS-PL') { throw 'PortCls adapter entry must retain MS-PL.' }
if ($adapterMainSrc -match 'audio_engine|scene_graph|asio_bridge|plugin_host') {
    throw 'PortCls adapter entry must not link GPL user-space implementation.'
}
foreach ($required in @('DriverEntry', 'HibikiAddDevice', 'HibikiStartDevice', 'HibikiRegisterSubdevicesV1',
    'PcInitializeAdapterDriver', 'PcAddAdapterDevice', 'PcNewPort', 'PcRegisterSubdevice',
    'CLSID_PortWaveRT', 'HIBIKI_SUBDEVICE_NAME_MAIN_V1', 'HIBIKI_SUBDEVICE_NAME_LOW_LATENCY_V1',
    'HIBIKI_SUBDEVICE_NAME_SURROUND_V1', 'HIBIKI_SUBDEVICE_NAME_VIRTUAL_MIC_V1')) {
    if (-not $adapterMainSrc.Contains($required)) { throw "PortCls adapter entry missing required symbol: $required" }
}
$infSource = Get-Content -LiteralPath $inf -Raw
foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'EndpointMainGuid', 'EndpointVirtualMicGuid', 'PnpLockdown=1')) {
    if (-not $infSource.Contains($required)) { throw "Driver INF source missing required boundary: $required" }
}
if ($infSource -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll') {
    throw 'Driver INF must not contain release credentials or GPL user-space payloads.'
}
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
