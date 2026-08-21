# Hibiki virtual driver

This directory will contain the SYSVAD-derived WaveRT/KS virtual endpoints.
The driver remains MS-PL and communicates with the GPL user-space engine only
through the versioned control contract. It must expose volume/mute nodes,
multichannel formats and stable identities from `config/distribution-profile.yml`.

Production driver loading on x64 Windows requires the Microsoft signing chain.
The public repository contains source and build instructions, never a signed
driver package.

The public ABI header is in `sdk/include/hibiki/driver_control_v1.h`. The driver
must include only this Apache-2.0 boundary plus its MS-PL implementation; it
must not link the GPL engine. Endpoint IDs and service names come from the
canonical distribution profile.

`include/hibiki/wavert_endpoint_state_v1.h` and
`src/wavert_endpoint_state.c` are the first WDK-facing control-state core. They
validate the supported LPCM formats, hold Q16.16 dB/mute/generation state and
apply the safety ceiling without allocation or COM. The future PortCls/KS
property handlers must call this core from the WaveRT miniport and publish the
result through the Apache ABI. The files are intentionally portable so CI can
test their invariants without pretending that a `.sys` has been built or
signed.

`wdk/hibiki_property_adapter.cpp` is the next MS-PL source boundary for a
WDK/SYSVAD-derived project. Run `pwsh -File tools/driver-source-check.ps1` to
verify that the scaffold retains its license and does not pull GPL user-space;
the check deliberately does not claim that a loadable driver exists. The
source-only INF template is in `inf/HibikiVirtualAudio.inf`; it references the
future signed SYS/CAT package and is not installable from this repository alone.

The WDK adapter entry points are endpoint-indexed and consume the fixed
`endpoint_topology_v1` catalog for render geometry, Windows channel mask and
property-state identity. A future SYSVAD/PortCls project must wire these
functions into its pin/property tables; this source boundary alone is not a
loadable, signed driver.

`include/hibiki/endpoint_topology_v1.h` and `src/endpoint_topology.c` fix the
first topology decision independently of WDK: Main is stereo render, Low
Latency is stereo render with a 64-frame default, Surround is 7.1 render with
the explicit Windows `0x63f` side/back channel mask, and Virtual Mic is stereo
capture. Every descriptor carries direction, default rate, supported-rate
flags, buffer size, channel mask and the permanent distribution GUID. The
catalog is a portable MS-PL input to the eventual SYSVAD tables; it is not a
PortCls miniport or a signed driver.

`include/hibiki/wavert_stream_v1.h` and `src/wavert_stream.c` provide the next
portable WaveRT data-path core: caller-owned byte storage, fixed LPCM Float32
frame geometry, single-producer/single-consumer ring operations, whole-block
overrun rejection and an underrun-safe silence fallback. It is deliberately
free of allocation, waits and GPL/user-space linkage. A future miniport must
provide the required synchronization and connect its pin callbacks to this
core; the source still does not constitute a loadable or signed `.sys`.
