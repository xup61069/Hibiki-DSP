let context = null;
let source = null;
let packetizer = null;
let destination = null;
let bridge = null;
let bridgeConnected = false;
let activeStream = null;
let capturing = false;
let droppedPackets = 0;
let totalPackets = 0;
let captureStartedAtMs = 0;
let lastPacketActivityAtMs = 0;
let bridgeRetryTimer = null;
let bridgeRetryAttempt = 0;
let bridgeRetryExhausted = false;
let bridgeRetryDeadlineMs = 0;
let stateHeartbeatTimer = null;
let captureLifecycleTail = Promise.resolve();

const BRIDGE_RETRY_MAX_ATTEMPTS = 10;
const BRIDGE_RETRY_BASE_MS = 1000;
const BRIDGE_RETRY_CAP_MS = 15000;
const STATE_HEARTBEAT_INTERVAL_MS = 1000;
const BRIDGE_RECONNECT_IDLE_V1 = 'idle';
const BRIDGE_RECONNECT_WAITING_V1 = 'waiting';
const BRIDGE_RECONNECT_RETRYING_V1 = 'retrying';
const BRIDGE_RECONNECT_CONNECTED_V1 = 'connected';
const BRIDGE_RECONNECT_EXHAUSTED_V1 = 'exhausted';

function bridgeRetryInSec() {
  return bridgeReconnectState() === BRIDGE_RECONNECT_WAITING_V1
    ? Math.max(0, Math.ceil((bridgeRetryDeadlineMs - Date.now()) / 1000))
    : 0;
}

function reportState() {
  chrome.runtime.sendMessage({
    type: 'capture-state',
    capturing: context !== null,
    bridgeConnected,
    droppedPackets,
    totalPackets,
    captureStartedAtMs,
    lastPacketActivityAtMs,
    bridgeReconnectState: bridgeReconnectState(),
    bridgeRetryInSec: bridgeRetryInSec(),
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
    const socket = new WebSocket('ws://127.0.0.1:17842/v1/tab');
    bridge = socket;
    socket.binaryType = 'arraybuffer';
    socket.onopen = () => {
      if (bridge !== socket || !capturing) return;
      bridgeRetryAttempt = 0;
      setStateHeartbeat(false);
      setBridgeConnected(true);
    };
    socket.onclose = () => {
      if (bridge !== socket || !capturing) return;
      bridge = null;
      setBridgeConnected(false);
      setStateHeartbeat(capturing);
      scheduleBridgeRetry();
    };
    socket.onerror = () => {};
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
  bridgeRetryDeadlineMs = Date.now() + delay;
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
  bridgeRetryDeadlineMs = 0;
  bridgeRetryAttempt = 0;
  bridgeRetryExhausted = false;
}

function enqueueCaptureLifecycle(operation) {
  const next = captureLifecycleTail.then(operation, operation);
  captureLifecycleTail = next.catch(() => {});
  return next;
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === 'get-capture-state') {
    sendResponse({
      capturing: context !== null,
      bridgeConnected,
      droppedPackets,
      totalPackets,
      captureStartedAtMs,
      lastPacketActivityAtMs,
      bridgeReconnectState: bridgeReconnectState(),
      bridgeRetryInSec: bridgeRetryInSec(),
    });
    return false;
  }
  if (message?.type === 'stop-tab-stream') {
    enqueueCaptureLifecycle(() => stopCapture())
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
  enqueueCaptureLifecycle(() => startCapture(message))
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
  await teardownCaptureGraph();
  capturing = true;
  droppedPackets = 0;
  totalPackets = 0;
  captureStartedAtMs = Date.now();
  lastPacketActivityAtMs = 0;
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
      track.addEventListener('ended', () => { void handleSourceEnded(stream); });
    });
    source = context.createMediaStreamSource(stream);
    packetizer = new AudioWorkletNode(context, 'hibiki-tab-packetizer');
    destination = context.createMediaStreamDestination();
    source.connect(packetizer).connect(destination);
    connectBridge();
    packetizer.port.onmessage = (event) => {
      lastPacketActivityAtMs = Date.now();
      if (bridge?.readyState === WebSocket.OPEN && event.data instanceof ArrayBuffer) {
        bridge.send(event.data);
        totalPackets++;
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

async function handleSourceEnded(endedStream) {
  if (activeStream !== endedStream) return;
  return enqueueCaptureLifecycle(async () => {
    if (activeStream !== endedStream) return;
    await teardownCaptureGraph();
    reportState();
    chrome.runtime.sendMessage({type: 'offscreen-capture-released'});
  });
}

async function teardownCaptureGraph() {
  capturing = false;
  cancelBridgeRetry();
  setStateHeartbeat(false);
  captureStartedAtMs = 0;
  lastPacketActivityAtMs = 0;
  const streamToStop = activeStream;
  activeStream = null;
  streamToStop?.getTracks().forEach(track => track.stop());
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
