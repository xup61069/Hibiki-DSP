# Hibiki ASIO bridge

The Hibiki ASIO bridge is a GPL user-space component. DAWs must select this
endpoint to receive Hibiki correction and Windows-linked Group Master volume.
Vendor ASIO clients remain outside the interception boundary.

`AsioBridgeModel` is the deterministic engine-side stream contract: it accepts
only 2/6/8-channel LPCM at 44.1/48/96/192 kHz, applies the canonical Group
Master once, and exposes a bounded interleaved process call with no allocation.

## Optional native transport

`HIBIKI_ENABLE_NATIVE_ASIO=ON` builds `hibiki_asio_native`, a GPL user-space
ASIO 2.x COM DLL. The SDK is not vendored; set `HIBIKI_ASIO_SDK_ROOT` to a
local checkout of the pinned open-source SDK listed in `THIRD_PARTY.yml`.
The target is off by default, so public CI and source-only releases never
produce or upload a DLL.

The transport exposes eight Float32 output channels, 32--4096 frame buffers
(128 preferred), 44.1/48/96/192 kHz, sample-position callbacks, and the stable
CLSID in `config/distribution-profile.yml`. `DllRegisterServer` follows the
ASIO SDK registry contract under `HKLM\\SOFTWARE\\ASIO` and
`HKCR\\CLSID`; local registration therefore requires an elevated developer
shell. It is unsigned developer output and does not yet claim physical sink
I/O; that boundary remains the Hibiki engine, IPC and the signed virtual
endpoint.

When the engine has created the stable `Local\\HibikiDSP\\v1\\asio` mapping,
`createBuffers` attaches the ASIO output blocks to the Apache-2.0
`hibiki_asio_transport_v1` SPSC ring. The DLL publishes the block after the
host callback returns; it never creates the mapping and never blocks the
callback on a missing engine. If the mapping is absent or its format does not
match, ASIO remains usable but is explicitly detached from Hibiki processing.
The engine-side `AsioTransportConsumerV1` owns mapping creation and performs
the allocation-free pop on its audio lane.
