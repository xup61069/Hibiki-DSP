# Out-of-process VST3 host

VST3 plug-ins run outside the RT engine with a watchdog, timeout policy,
latency reporting and crash quarantine. A plug-in failure must never stop
other lanes or the physical output sinks.

`PluginHostModel` provides the control-plane contract: only trusted, certified,
same-channel-count descriptors can enter `Running`; a crash or missed heartbeat
deadline moves the lane to `Quarantined` and blocks further processing. Its
passthrough is a deterministic test fixture, not a VST3 implementation; the
actual SDK host remains isolated behind this boundary.
