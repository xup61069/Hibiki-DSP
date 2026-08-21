# WDK PortCls adapter boundary

`hibiki_property_adapter.cpp` is an MS-PL WDK-only scaffold for the eventual
SYSVAD-derived topology. It dispatches `KSPROPERTY_AUDIO_VOLUMELEVEL` and
`KSPROPERTY_AUDIO_MUTE` into the portable `wavert_endpoint_state_v1` core,
preserving the Q16.16 dB safety ceiling, mute and monotonic generation rules.
The request boundary rejects missing instance/value buffers and verbs that are
neither a single GET nor SET before dereferencing the KS property request.

It is deliberately not part of the default CMake target: this machine does
not have the locked WDK 10.0.28000.2526 and no `.sys` is produced. A future WDK
adapter must provide one context per endpoint, wire the functions into the
SYSVAD property/automation tables, expose the fixed LPCM pin formats and use
the Apache driver-control ABI over IPC. It must not include or link GPL
user-space code. The portable `wavert_stream_v1` core is the intended ring/
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
result or Microsoft signature.

Before enabling a driver build, the maintainer must compile this source in a
clean WDK project, run Driver Verifier/HLK and record signability evidence for
Windows 11 24H2+; source presence alone is not driver evidence.
