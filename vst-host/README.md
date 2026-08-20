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

The catalog is control-plane only. `vst3_sdk_processor.hpp`/`.cpp` now add an
optional worker-side adapter for one main input/output bus: it initializes a
selected class, accepts 1/2/5.1/7.1 layouts, uses fixed 4096-frame planar
scratch buffers, exposes plugin-reported latency and fails closed on invalid
or non-finite output. The adapter is deliberately not linked into the normal
engine or RT graph. `hibiki_vst3_sdk_worker` wires that adapter to the existing
bounded worker pipe and ProcessBlock frame contract; it is built only when the
local pinned SDK is supplied. Supervisor launch policy, parameter automation,
latency-compensation policy, crash-dump redaction and real plugin certification
remain separate release gates.
