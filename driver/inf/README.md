# Driver INF source boundary

`HibikiVirtualAudio.inf` is the public, MS-PL source template for the fixed
Root\HibikiDSP hardware identity and four endpoint GUID values. It references
the future `HibikiVirtualAudio.sys` and catalog, but those binary package files
are deliberately not committed. The INF is therefore not installable from a
fresh Git clone and is not evidence of a working WaveRT miniport.

The project does not require HLK or any signing. A maintainer may build this
source with a locked WDK for local verification, but release acceptance still
requires real target-machine PortCls/audio validation. The SYS must retain the
MS-PL boundary and communicate with GPL user-space only through the versioned
Apache ABI/IPC.

The source-only repository can run `tools/driver-source-check.ps1`; this only
verifies the INF contract. Local builds and generated binaries remain outside
GitHub.

The first channel-mask/topology decision is recorded in
`driver/include/hibiki/endpoint_topology_v1.h`; the eventual INF/KS topology
must consume those descriptors rather than infer channel ordering from a
channel count.
