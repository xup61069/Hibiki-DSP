# WDK PortCls adapter boundary

`hibiki_property_adapter.cpp` is an MS-PL WDK-only scaffold for the eventual
SYSVAD-derived topology. It dispatches `KSPROPERTY_AUDIO_VOLUMELEVEL` and
`KSPROPERTY_AUDIO_MUTE` into the portable `wavert_endpoint_state_v1` core,
preserving the Q16.16 dB safety ceiling, mute and monotonic generation rules.

It is deliberately not part of the default CMake target: this machine does
not have the locked WDK 10.0.28000.2526 and no `.sys` is produced. A future WDK
adapter must provide one context per endpoint, wire the functions into the
SYSVAD property/automation tables, expose the fixed LPCM pin formats and use
the Apache driver-control ABI over IPC. It must not include or link GPL
user-space code. The portable `wavert_stream_v1` core is the intended ring/
underrun boundary for those pin callbacks; WDK code must add the required
interlocked producer/consumer publication around it.

Before enabling a driver build, the maintainer must compile this source in a
clean WDK project, run Driver Verifier/HLK and record signability evidence for
Windows 11 24H2+; source presence alone is not driver evidence.
