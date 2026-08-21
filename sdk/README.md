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
caller-owned slot is not silently accepted.
