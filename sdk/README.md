# Hibiki public SDK

Public schemas and IDL are Apache-2.0. SDK versioning follows the contracts in
`schemas/`; breaking changes require a new schema version and migration note.

`include/hibiki/driver_control_v1.h` is the first C-compatible ABI surface. It
uses fixed-width fields and Q16.16 dB values so the eventual WaveRT driver and
user-space broker do not share C++ layout or allocator assumptions.
