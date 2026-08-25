let context = null;
let source = null;
let packetizer = null;
let destination = null;
let bridge = null;
let bridgeConnected = false;
let activeStream = null;
let capturing = false;
let droppedPackets = 0;
let bridgeRetryTimer = null;
let bridgeRetryAttempt = 0;
let bridgeRetryExhausted = false;
let stateHeartbeatTimer = null;

const BRIDGE_RETRY_MAX_ATTEMPTS = 10;
const BRIDGE_RETRY_BASE_MS = 1000;
const BRIDGE_RETRY_CAP_MS = 15000;
const STATE_HEARTBEAT_INTERVAL_MS = 1000;
const BRIDGE_RECONNECT_IDLE_V1 = 'idle';
const BRIDGE_RECONNECT_WAITING_V1 = 'waiting';
const BRIDGE_RECONNECT_RETRYING_V1 = 'retrying';
const BRIDGE_RECONNECT_CONNECTED_V1 = 'connected';
const BRIDGE_RECONNECT_EXHAUSTED_V1 = 'exhausted';

function reportState() {
  chrome.runtime.sendMessage({
    type: 'capture-state',
    capturing: context !== null,
    bridgeConnected,
    droppedPackets,
    bridgeReconnectState: bridgeReconnectState(),
  });
}

function bridgeReconnectState() {
  if (bridgeConnected) return BRIDGE_RECONNECT_CONNECTED_V1;
  if (!capturing) return BRIDGE_RECONNECT_IDLE_V1;
  if (bridgeRetryTimer !== null) return BRIDGE_RECONNECT_WAITING_V1;
  return bridgeRetryExhausted ? BRIDGE_RECONNECT_EXHAUSTED_V1 : BRIDGE_RECONNECT_RETRYING_V1;
}

function setStateHeartbeat(enabled) {
  if (enabled && stateHeartbeatTimer === null) {
    stateHeartbeatTimer = setInterval(reportState, STATE_HEARTBEAT_INTERVAL_MS);
  } else if (!enabled && stateHeartbeatTimer !== null) {
    clearInterval(stateHeartbeatTimer);
    stateHeartbeatTimer = null;
  }
}

function connectBridge() {
  if (!capturing || bridge) return;
  try {
    bridge = new WebSocket('ws://127.0.0.1:17842/v1/tab');
    bridge.binaryType = 'arraybuffer';
    bridge.onopen = () => {
      bridgeRetryAttempt = 0;
      setStateHeartbeat(false);
      setBridgeConnected(true);
    };
    bridge.onclose = () => {
      bridge = null;
      setBridgeConnected(false);
      setStateHeartbeat(true);
      scheduleBridgeRetry();
    };
    bridge.onerror = () => {};
  } catch (_) {
    bridge = null;
    scheduleBridgeRetry();
  }
}

function scheduleBridgeRetry() {
  if (!capturing || bridgeRetryTimer !== null) return;
  if (bridgeRetryAttempt >= BRIDGE_RETRY_MAX_ATTEMPTS) {
    bridgeRetryExhausted = true;
    reportState();
    return;
  }
  bridgeRetryExhausted = false;
  const delay = Math.min(BRIDGE_RETRY_BASE_MS * Math.pow(2, bridgeRetryAttempt), BRIDGE_RETRY_CAP_MS);
  bridgeRetryAttempt++;
  bridgeRetryTimer = setTimeout(() => {
    bridgeRetryTimer = null;
    connectBridge();
  }, delay);
}

function cancelBridgeRetry() {
  if (bridgeRetryTimer !== null) {
    clearTimeout(bridgeRetryTimer);
    bridgeRetryTimer = null;
  }
  bridgeRetryAttempt = 0;
  bridgeRetryExhausted = false;
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === 'get-capture-state') {
    sendResponse({
      capturing: context !== null,
      bridgeConnected,
      droppedPackets,
      bridgeReconnectState: bridgeReconnectState(),
    });
    return false;
  }
  if (message?.type === 'stop-tab-stream') {
    stopCapture()
      .then(() => sendResponse({ok: true}))
      .catch((error) => sendResponse({ok: false, error: String(error)}));
    return true;
  }
  if (message?.type === 'retry-tab-bridge') {
    if (!capturing || bridgeConnected) {
      sendResponse({ok: false, error: 'bridge-retry-unavailable'});
      return false;
    }
    cancelBridgeRetry();
    connectBridge();
    sendResponse({ok: true});
    return false;
  }
  if (message?.type !== 'start-tab-stream' || typeof message.streamId !== 'string') return false;
  startCapture(message)
    .then(() => sendResponse({ok: true}))
    .catch((error) => sendResponse({ok: false, error: String(error)}));
  return true;
});

function setBridgeConnected(connected) {
  const changed = bridgeConnected !== connected;
  bridgeConnected = connected;
  if (changed) reportState();
}

async function startCapture(message) {
  capturing = true;
  droppedPackets = 0;
  cancelBridgeRetry();
  bridge?.close();
  bridge = null;
  setBridgeConnected(false);
  await closeExistingContext();
  context = new AudioContext();
  try {
    await context.audioWorklet.addModule('audio-worklet.js');
    const constraints = {
      audio: {
      mandatory: {chromeMediaSource: 'tab', chromeMediaSourceId: message.streamId}
      },
      video: false
    };
    const stream = await navigator.mediaDevices.getUserMedia(constraints);
    activeStream = stream;
    stream.getTracks().forEach((track) => {
      track.addEventListener('ended', () => { void handleSourceEnded(); });
    });
    source = context.createMediaStreamSource(stream);
    packetizer = new AudioWorkletNode(context, 'hibiki-tab-packetizer');
    destination = context.createMediaStreamDestination();
    source.connect(packetizer).connect(destination);
    connectBridge();
    packetizer.port.onmessage = (event) => {
      if (bridge?.readyState === WebSocket.OPEN && event.data instanceof ArrayBuffer) {
        bridge.send(event.data);
      } else {
        droppedPackets++;
      }
    };
    await context.resume();
    reportState();
  } catch (error) {
    await teardownCaptureGraph();
    reportState();
    throw error;
  }
}

async function handleSourceEnded() {
  if (!activeStream) return;
  await teardownCaptureGraph();
  reportState();
  chrome.runtime.sendMessage({type: 'offscreen-capture-released'});
}

async function closeExistingContext() {
  if (context) {
    try { await context.close(); } catch (_) {}
    context = null;
  }
}

async function teardownCaptureGraph() {
  capturing = false;
  cancelBridgeRetry();
  setStateHeartbeat(false);
  activeStream?.getTracks().forEach(track => track.stop());
  activeStream = null;
  source?.disconnect();
  source = null;
  packetizer?.disconnect();
  packetizer = null;
  destination?.disconnect();
  destination = null;
  bridge?.close();
  bridge = null;
  bridgeConnected = false;
  if (context) {
    try { await context.close(); } catch (_) {}
    context = null;
  }
}

async function stopCapture() {
  await teardownCaptureGraph();
  reportState();
}
