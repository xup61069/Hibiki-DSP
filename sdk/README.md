# Hibiki public SDK

Public schemas and IDL are Apache-2.0. SDK versioning follows the contracts in
`schemas/`; breaking changes require a new schema version and migration note.

`include/hibiki/driver_control_v1.h` is the first C-compatible ABI surface. It
uses fixed-width fields and Q16.16 dB values so the eventual WaveRT driver and
user-space broker do not share C++ layout or allocator assumptions.

`include/hibiki/driver_stream_transport_v1.h` adds the bounded little-endian
Float32 packet ABI for driver-to-engine render/capture blocks. Its C encoder,
validator and payload view are allocation-free and fixed-layout; the GPL
engine consumes them through `driver_stream_bridge.hpp`, while a future
MS-PL/WaveRT miniport may produce the same packet from its pin callback. The
packet span passed to validation must be exactly `header.size_bytes`; a larger
caller-owned slot is not silently accepted. `sequence` and `generation` are
non-zero freshness and lifecycle fields; the encoder and validator reject zero
for either field before accepting the payload, while `UINT64_MAX` remains valid.

`include/hibiki/driver_control_transport_v1.h` adds the corresponding fixed
136-byte little-endian control packet for `endpoint-state` and
`volume-notification`. It writes every field at an explicit byte offset,
validates Q16.16 dB/rate/channel/generation bounds and decodes into caller-owned
storage without relying on compiler struct padding. The GPL engine consumes it
through `driver_control_bridge.hpp`; the bridge applies only the requested dB
and mute through the engine's canonical safety path and can ignore registered
event-context values to prevent feedback loops. This is a control-plane ABI,
not evidence of a loadable WaveRT driver or a signed IPC transport.

The same header exposes a 16-byte little-endian header-only Hello/Ack/Error
framing path for request correlation. It accepts the five message types from
`driver_control_v1.h`, but intentionally carries no private error text or
unbounded payload in v1; a future payload needs a new versioned contract.
