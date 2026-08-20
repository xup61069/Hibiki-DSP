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
