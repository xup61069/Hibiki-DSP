# Out-of-process VST3 host

VST3 plug-ins run outside the RT engine with a watchdog, timeout policy,
latency reporting and crash quarantine. A plug-in failure must never stop
other lanes or the physical output sinks.

`PluginHostModel` provides the control-plane contract: only trusted, certified,
same-channel-count descriptors can enter `Running`; a crash or missed heartbeat
deadline moves the lane to `Quarantined` and blocks further processing. The
source-only `hibiki_vst_worker` executable now exercises the bounded worker
protocol with Hello/Heartbeat/ProcessBlock passthrough/Shutdown. It remains a
transport fixture: plugin dispatch and certification are intentionally isolated
behind this boundary. When a local checkout of the pinned SDK in `THIRD_PARTY.yml`
is available, the optional `hibiki_vst3_sdk_catalog` target can scan module
factory metadata without putting SDK source or binaries in the repository:

`cmake -S . -B .local/vst3-build -DHIBIKI_ENABLE_VST3_SDK=ON -DHIBIKI_VST3_SDK_ROOT=<path>`

The catalog is control-plane only; actual parameter/audio dispatch, latency
compensation and worker integration remain separate release gates.
