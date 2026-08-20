# Chrome/Edge extension

The MV3 extension starts single-tab capture only after an explicit user gesture.
Captured audio is handed to a native lane; the extension never receives private
Hibiki signing or Gumroad credentials.

The source includes a minimal MV3 popup, service worker and offscreen document.
`tabCapture.getMediaStreamId` is requested only from the popup click path. The
offscreen graph keeps the user-selected stream alive after the popup closes and
an AudioWorklet emits versioned `HIBT` little-endian Float32 packets to the
optional localhost bridge (`ws://127.0.0.1:17842/v1/tab`). If that bridge is
not running, the browser stream still plays normally and packets are dropped;
the extension never silently captures a tab or microphone.

Packet header: magic `HIBT`, version `1`, channel count, frame count and
sample rate (all little-endian), followed by interleaved Float32 samples. The
source-only `hibiki_tab_bridge_contract` library now performs that packet
validation (including finite-sample checks) before the loopback WebSocket
receiver invokes its control-thread callback. `process_tab_capture_lane_v1` is
the bounded adapter from that queue into `AudioEngineModel::process_lane_block`;
it can optionally apply the per-tab, slow RMS-proxy level controller after a
sample-rate check. A caller can also provide up to 16 fixed-capacity RBJ PEQ
filters before the level controller; mismatched sample rate/channel settings
fail closed. Noise-reduction models and their provenance remain separate
boundaries, and the default path is unchanged when no effects are supplied.
