# WDK PortCls adapter boundary

`hibiki_property_adapter.cpp` is an MS-PL WDK-only scaffold for the eventual
SYSVAD-derived topology. It dispatches `KSPROPERTY_AUDIO_VOLUMELEVEL` and
`KSPROPERTY_AUDIO_MUTE` into the portable `wavert_endpoint_state_v1` core,
preserving the Q16.16 dB safety ceiling, mute and monotonic generation rules.
The request boundary rejects missing instance/value buffers and verbs that are
neither a single GET nor SET before dereferencing the KS property request.

`hibiki_miniport_wavert.h` and `hibiki_miniport_wavert.cpp` provide the
PortCls COM interface adapter implementations (`IMiniportWaveRT` via
`HibikiMiniportWaveRtV1` and the notification stream interface
`IMiniportWaveRTStreamNotification` via `HibikiMiniportWaveRtStreamV1`).
They wire notification-aware audio buffer allocation
(`AllocateBufferWithNotification`), event notifications
(`RegisterNotificationEvent`/`UnregisterNotificationEvent`), latency
reporting (`GetHWLatency`), position queries (`GetPosition`), stream state
transitions (`SetState`) and property context binding into the portable
`endpoint_topology_v1`, `wavert_stream_v1`, and `wavert_endpoint_state_v1`
cores. They retain strict MS-PL licensing, zero GPL linkage, and non-allocating
real-time audio paths.

`hibiki_filter_tables.h` and `hibiki_filter_tables.cpp` provide the
PortCls filter descriptor tables (`PCFILTER_DESCRIPTOR`), KS pin descriptors
(`PCPIN_DESCRIPTOR`), KS node descriptors (`PCNODE_DESCRIPTOR` for volume
and mute), connection descriptors (`PCCONNECTION_DESCRIPTOR`), and KS property
automation tables for all four endpoints (Main, Low Latency, Surround 7.1,
and Virtual Mic). They wire `GetDescription` and `DataRangeIntersection` to
static, allocation-free filter topologies matching `endpoint_topology_v1`.

`hibiki_adapter.h` and `hibiki_adapter.cpp` provide the PortCls adapter
driver entry point (`DriverEntry`), device object initialization
(`HibikiAddDevice`), subdevice registration (`HibikiStartDevice` calling
`PcRegisterSubdevice` for `PortWaveRT`), and PNP/Power dispatch
(`HibikiPnpDispatchV1` / `HibikiPowerDispatchV1`). They bind all four endpoints
to the Windows audio driver life cycle without linking GPL user-space code.

It is deliberately not part of the default CMake target: this machine does
not have the locked WDK 10.0.28000.2526 and no `.sys` is produced. A future WDK
adapter must provide one context per endpoint, wire the functions into the
SYSVAD property/automation tables, expose the fixed LPCM pin formats and use
the Apache driver-control ABI over IPC. For endpoint-state and volume
notifications, use `hibiki_driver_control_transport_v1.h`'s explicit 136-byte
little-endian encoder rather than copying a C struct; the future driver may
share that Apache C codec without linking GPL code. It must not include or link
GPL user-space code. The portable `wavert_stream_v1` core is the intended ring/
underrun boundary for those pin callbacks; WDK code must add the required
interlocked producer/consumer publication around it. The companion
`hibiki_stream_adapter.cpp` demonstrates that WDK boundary with a spin lock,
render submit, underrun-safe render read and reset; it is source-only until
compiled inside a SYSVAD/PortCls project.

The endpoint-indexed entry points consume the fixed `endpoint_topology_v1`
catalog rather than accepting free-form channel/rate values: render pin
initialization uses catalog buffer geometry, format construction emits the
catalog channel mask, and property-context initialization uses the catalog
GUID/channel/rate. This keeps the eventual SYSVAD tables and portable contract
on one identity source. It still does not provide a WDK build, `.sys`, HLK
result or Microsoft signature. `HibikiWaveRtPinInitializeCaptureEndpointV1`
and the generic `HibikiWaveRtBuildFormatEndpointV1` cover the Virtual Mic
capture direction without creating a second format contract.

Before enabling a driver build, the maintainer must compile this source in a
clean WDK project, run Driver Verifier/HLK and record signability evidence for
Windows 11 24H2+; source presence alone is not driver evidence.

The Apache transport also provides a 16-byte header-only Hello/Ack/Error
exchange for request correlation. It is suitable for a future bounded kernel/
user control channel, but does not define an unbounded error payload or replace
the target WDK IPC implementation.
