# Out-of-process VST3 host

VST3 plug-ins run outside the RT engine with a watchdog, timeout policy,
latency reporting and crash quarantine. A plug-in failure must never stop
other lanes or the physical output sinks.

`PluginHostModel` now provides the control-plane contract: only trusted,
same-channel-count descriptors can enter `Running`; a crash moves the lane to
`Quarantined` and blocks further processing. Its passthrough is a deterministic
test fixture, not a VST3 implementation; the actual SDK host remains isolated
behind this boundary.
