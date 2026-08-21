# Driver INF source boundary

`HibikiVirtualAudio.inf` is the public, MS-PL source template for the fixed
Root\HibikiDSP hardware identity and four endpoint GUID values. It references
the future `HibikiVirtualAudio.sys` and catalog, but those signed package files
are deliberately not committed. The INF is therefore not installable from a
fresh Git clone and is not evidence of a working WaveRT miniport.

Before a release, a maintainer must build it with the locked WDK, run
`Inf2Cat` for the supported Windows 11 targets, integrate the SYSVAD-derived
topology/property tables, submit the exact CAB to Microsoft, and record the
returned signature in the release manifest. The SYS must retain the MS-PL
boundary and communicate with GPL user-space only through the versioned
Apache ABI/IPC.

The source-only repository can run `tools/driver-signability-check.ps1` without
a package; this only verifies the INF contract. On a target WDK machine, pass
the private build directory to `-PackageRoot -RequireInf2Cat` to run Inf2Cat for
`10_GE_X64` and `10_25H2_X64`. The generated SYS/CAT remain outside GitHub and
must still go through Microsoft signing and HLK/WHCP validation.

The first channel-mask/topology decision is recorded in
`driver/include/hibiki/endpoint_topology_v1.h`; the eventual INF/KS topology
must consume those descriptors rather than infer channel ordering from a
channel count.
