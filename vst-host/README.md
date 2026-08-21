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
local pinned SDK is supplied. `Vst3SandboxLaunchV1` can select the SDK worker
with an explicit class UID, sample rate and 2/6/8-channel layout; empty class
UID preserves the existing passthrough worker, and invalid launch fields are
rejected before process creation. Parameter automation, latency-compensation
policy, crash-dump redaction and real plugin certification remain separate gates.

The processor API and optional SDK worker accept up to 16 parameter IDs, five
sample-accurate points per ID and normalized values in `[0,1]`; the bounded
`ProcessBlockWithParameters` frame converts them to the SDK's
`IParameterChanges` before `process`. The bounded timeline and Scene scheduler
are part of this source baseline; supervisor UI editing and full end-to-end
automation remain separate gates.

The supervisor now exposes `handshake_worker` and `process_worker_block` as the
only control-plane exchange calls. They validate HelloAck/response IDs, channel
and frame shape, payload size and finite output, and clear the caller's output
before a failed exchange. They may wait for the bounded named-pipe timeout and
must never be called from the RT graph; a failed worker is reported to the
existing quarantine policy rather than silently restarted.

`Vst3WorkerLaneSessionV1` is the next control-plane layer: it binds a stable lane
token and reported latency, requires a successful handshake, extracts events
from `Vst3ParameterTimelineV1`, and rejects non-contiguous blocks. A worker or
ordering failure moves that lane to `Degraded`; Scene scheduling and
back-pressure are bounded source contracts, while third-party certification
remains a follow-up gate.

`PluginHostModel` now exposes the host-model entry points for preparing,
handshaking and processing that lane. Only the existing trusted/certified,
same-layout descriptor can enter the session; a failed exchange detaches the
lane and moves the host to `Quarantined`. This is still a source-level contract,
not evidence of a signed driver or a certified third-party plug-in.

`Vst3SceneAutomationSchedulerV1` stores up to 16 stable timeline IDs and
scene/lane bindings, validates the complete scene before activation, and
rejects concurrent blocks per lane with an explicit `busy` result. It applies
timeline snapshots to worker lanes but deliberately does not serialize opaque
plugin state. `Vst3SceneStateCoordinatorV1` separately binds up to 16 Scene
state references, checks private metadata and approved migration rules, and
restores only into caller-owned buffers.

`Vst3PluginStateStoreV1` provides that boundary without publishing state bytes:
it stores at most 16 private caller-owned blobs (1 MiB each), binds them to the
plugin/class/module SHA-256 identity and a state version, and refuses restore on
identity, version or destination-size mismatch. Version changes require an
explicit identity-checked migration handler from the fixed registry; there is
no automatic migration or public state serializer.

When a local pinned SDK checkout is supplied, the optional SDK processor also
uses a bounded `IBStream` for component `getState/setState`, with separate
overflow, plugin-error and destination-size results. This target remains
unsigned/local and is not a third-party compatibility certification.

`LatencyAlignmentPlanV1` and `FixedDelayLineV1` provide a bounded 16,384-sample
alignment plan and fixed 8-channel delay primitive. They are tested separately
from supervisor and graph lane commit, so plugin certification and full latency
policy are still pending.
