# Chrome/Edge extension

The MV3 extension starts single-tab capture only after an explicit user gesture.
Captured audio is handed to a native lane; the extension never receives private
Hibiki credentials.

The source includes a minimal MV3 popup, service worker and offscreen document.
`tabCapture.getMediaStreamId` is requested only from the popup click path. The
offscreen graph keeps the user-selected stream alive after the popup closes and
an AudioWorklet emits versioned `HIBT` little-endian Float32 packets to the
optional localhost bridge (`ws://127.0.0.1:17842/v1/tab`). If that bridge is
not running, the browser stream still plays normally and packets are dropped;
the extension never silently captures a tab or microphone.

The popup exposes explicit Start and Stop controls. It queries the real capture
state when opened, reports start/stop failures instead of pretending success,
and shows whether the optional loopback native bridge is currently connected.
While capturing without a bridge, it distinguishes waiting for the next retry,
an active retry, and exhaustion of the bounded retry budget. If retries are
exhausted, the popup offers an explicit manual retry button that resets the
bounded budget without stopping capture or rebuilding the audio graph. The
popup also refreshes the dropped-packet count once per second while capture is
active and the bridge is disconnected, so a rising number shows that tab audio
is still flowing and packets are still being dropped; the heartbeat stops as
soon as the bridge reconnects. The retry only re-attempts the optional loopback
bridge: local tab playback remains unchanged, packets continue to be dropped
until the bridge reconnects, and the retry and heartbeat are diagnostic only —
they do not imply browser or vendor control. Stop
releases the user-selected stream and capture graph. Closing or navigating away
from the captured tab also ends the source stream and releases that graph.

Packet header: magic `HIBT`, version `1`, channel count, frame count and
sample rate (all little-endian), followed by interleaved Float32 samples. The
source-only `hibiki_tab_bridge_contract` library now performs that packet
validation (including finite-sample checks) before the loopback WebSocket
receiver invokes its control-thread callback. `process_tab_capture_lane_v1` is
the bounded adapter from that queue into `AudioEngineModel::process_lane_block`;
it can optionally apply the per-tab, slow RMS-proxy level controller after a
sample-rate check. A caller can also provide up to 16 fixed-capacity RBJ PEQ
filters before the level controller; mismatched sample rate/channel settings
fail closed. A caller may also supply a fixed-capacity mono or per-channel IR
kernel (up to 4096 taps) after PEQ and before level correction; the declared
phase delay is metadata that still requires measurement. Noise-reduction models and their provenance remain separate
boundaries. A separate optional basic suppressor provides a bounded high-pass/downward gate;
it is not RNNoise, AI denoising or AEC. The default path is unchanged when no effects are supplied.
`process_tab_capture_lane_to_wasapi_v1` reuses those effects and submits the processed block once
to the same dual-worker WASAPI handoff used by ASIO and driver lanes; an unbound sink fails closed.
