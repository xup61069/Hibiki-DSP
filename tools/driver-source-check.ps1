[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function Assert-DriverSourcePolicy([hashtable]$files, [string]$sourceName) {
  foreach ($requiredFile in @('property-adapter', 'topology', 'stream', 'stream-adapter', 'inf')) {
    if (-not $files.ContainsKey($requiredFile)) {
      throw "WDK source fixture is missing [$requiredFile]: $sourceName"
    }
  }

  $source = $files['property-adapter']
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

  $topology = $files['topology']
  if ($topology -notmatch 'HIBIKI_CHANNEL_MASK_71_V1' -or
      $topology -notmatch 'HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1' -or
      $topology -notmatch '8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10' -or
      $topology -notmatch 'hibiki_endpoint_topology_validate_v1') {
    throw 'Endpoint topology catalog is missing the fixed channel-mask/GUID contract.'
  }

  $stream = $files['stream']
  foreach ($required in @('HIBIKI_WAVERT_STREAM_ABI_V1', 'hibiki_wavert_stream_push_v1',
      'hibiki_wavert_stream_pop_or_silence_v1', 'dropped_frames', 'underrun_frames')) {
    if (-not $stream.Contains($required)) { throw "WaveRT stream core missing required boundary: $required" }
  }
  if ($stream -match '(?i)malloc|calloc|realloc|free|CreateThread|KeWaitFor') {
    throw 'WaveRT stream core must remain allocation-free and non-blocking.'
  }

  $adapterSource = $files['stream-adapter']
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

  foreach ($required in @('HibikiPropertyContextInitializeEndpointV1',
      'hibiki_endpoint_topology_get_v1', 'hibiki_wavert_endpoint_state_init_v1')) {
    if (-not $source.Contains($required)) { throw "WDK property adapter missing endpoint initialization: $required" }
  }

  $infSource = $files['inf']
  foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'EndpointMainGuid',
      'EndpointVirtualMicGuid', 'PnpLockdown=1')) {
    if (-not $infSource.Contains($required)) { throw "Driver INF source missing required boundary: $required" }
  }
  if ($infSource -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll') {
    throw 'Driver INF must not contain release credentials or GPL user-space payloads.'
  }
}

function Get-RepositoryDriverSources([string]$repo) {
  $paths = @{
    'property-adapter' = 'driver/wdk/hibiki_property_adapter.cpp'
    'topology' = 'driver/include/hibiki/endpoint_topology_v1.h'
    'stream' = 'driver/include/hibiki/wavert_stream_v1.h'
    'stream-adapter' = 'driver/wdk/hibiki_stream_adapter.cpp'
    'inf' = 'driver/inf/HibikiVirtualAudio.inf'
  }
  $files = @{}
  foreach ($entry in $paths.GetEnumerator()) {
    $path = Join-Path $repo $entry.Value
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing WDK source boundary: $path" }
    $files[$entry.Key] = Get-Content -LiteralPath $path -Raw
  }
  $files['topology'] += Get-Content -LiteralPath (Join-Path $repo 'driver/src/endpoint_topology.c') -Raw
  $files['stream'] += Get-Content -LiteralPath (Join-Path $repo 'driver/src/wavert_stream.c') -Raw
  foreach ($readme in @('driver/wdk/README.md', 'driver/inf/README.md')) {
    $readmePath = Join-Path $repo $readme
    if (-not (Test-Path -LiteralPath $readmePath)) { throw "Missing WDK source boundary: $readmePath" }
  }
  return ,$files
}

if ($SelfTest) {
  $valid = @{
    'property-adapter' = @'
SPDX-License-Identifier: MS-PL
KSPROPERTY_AUDIO_VOLUMELEVEL KSPROPERTY_AUDIO_MUTE
KSPROPERTY_TYPE_BASICSUPPORT KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET STATUS_BUFFER_TOO_SMALL
HibikiPropertyContextInitializeEndpointV1 hibiki_endpoint_topology_get_v1 hibiki_wavert_endpoint_state_init_v1
'@
    'topology' = @'
HIBIKI_CHANNEL_MASK_71_V1 HIBIKI_ENDPOINT_VIRTUAL_MIC_CAPTURE_V1
8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10 hibiki_endpoint_topology_validate_v1
'@
    'stream' = @'
HIBIKI_WAVERT_STREAM_ABI_V1 hibiki_wavert_stream_push_v1
hibiki_wavert_stream_pop_or_silence_v1 dropped_frames underrun_frames
'@
    'stream-adapter' = @'
KeAcquireSpinLock KeReleaseSpinLock HibikiWaveRtPinSubmitRenderV1
HibikiWaveRtPinReadRenderV1 HibikiWaveRtPinInitializeEndpointV1
HibikiWaveRtPinInitializeCaptureEndpointV1 HibikiWaveRtBuildFormatEndpointV1
HibikiWaveRtBuildFormatV1 KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
hibiki_wavert_stream_pop_or_silence_v1
'@
    'inf' = @'
Root\HibikiDSP HibikiVirtualAudio.sys EndpointMainGuid EndpointVirtualMicGuid PnpLockdown=1
'@
  }
  Assert-DriverSourcePolicy $valid 'selftest-valid-fixture'

  $fixtures = @(
    @{ Name = 'missing-ms-pl'; Key = 'property-adapter'; Old = 'SPDX-License-Identifier: MS-PL'; New = 'SPDX-License-Identifier: GPL-3.0' },
    @{ Name = 'gpl-user-space-link'; Key = 'property-adapter'; Old = 'SPDX-License-Identifier: MS-PL'; New = 'SPDX-License-Identifier: MS-PL audio_engine' },
    @{ Name = 'allocation-or-blocking'; Key = 'stream-adapter'; Old = 'KeAcquireSpinLock'; New = 'KeAcquireSpinLock malloc' },
    @{ Name = 'missing-topology-boundary'; Key = 'topology'; Old = 'HIBIKI_CHANNEL_MASK_71_V1'; New = 'MISSING_CHANNEL_MASK' },
    @{ Name = 'missing-stream-boundary'; Key = 'stream'; Old = 'HIBIKI_WAVERT_STREAM_ABI_V1'; New = 'MISSING_STREAM_ABI' },
    @{ Name = 'missing-stream-adapter-boundary'; Key = 'stream-adapter'; Old = 'HibikiWaveRtPinSubmitRenderV1'; New = 'MISSING_RENDER_SUBMIT' },
    @{ Name = 'missing-property-boundary'; Key = 'property-adapter'; Old = 'HibikiPropertyContextInitializeEndpointV1'; New = 'MISSING_PROPERTY_INIT' },
    @{ Name = 'missing-inf-boundary'; Key = 'inf'; Old = 'Root\HibikiDSP'; New = 'Root\OtherDSP' },
    @{ Name = 'forbidden-inf-payload'; Key = 'inf'; Old = 'HibikiVirtualAudio.sys'; New = 'HibikiDSP.dll' }
  )
  foreach ($fixture in $fixtures) {
    $copy = @{}
    foreach ($entry in $valid.GetEnumerator()) { $copy[$entry.Key] = $entry.Value }
    $copy[$fixture.Key] = $copy[$fixture.Key].Replace($fixture.Old, $fixture.New)
    $caught = $false
    try { Assert-DriverSourcePolicy $copy "selftest-$($fixture.Name)" } catch { $caught = $true }
    if (-not $caught) { throw "Driver source self-test expected rejection: $($fixture.Name)" }
  }
  Write-Output "WDK source boundary self-test passed ($($fixtures.Count + 1) cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
Assert-DriverSourcePolicy (Get-RepositoryDriverSources $repo) $repo
Write-Output 'WDK source boundary checks passed (source-only; no .sys build).'
