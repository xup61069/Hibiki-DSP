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
validation (including finite-sample checks) before a future WebSocket receiver
hands data to a lane. The receiver, lane routing and noise-reduction DSP remain
separate boundaries.
