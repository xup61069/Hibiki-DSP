# Out-of-process VST3 host

VST3 plug-ins run outside the RT engine with a watchdog, timeout policy,
latency reporting and crash quarantine. A plug-in failure must never stop
other lanes or the physical output sinks.

`PluginHostModel` provides the control-plane contract: only trusted, certified,
same-channel-count descriptors can enter `Running`; a crash or missed heartbeat
deadline moves the lane to `Quarantined` and blocks further processing. The
source-only `hibiki_vst_worker` executable now exercises the bounded worker
protocol with Hello/Heartbeat/ProcessBlock passthrough/Shutdown. It remains a
transport fixture: VST3 SDK loading, plugin dispatch, scan and certification
are intentionally isolated behind this boundary.
