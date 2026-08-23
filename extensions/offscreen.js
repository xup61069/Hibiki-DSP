let context = null;
let source = null;
let packetizer = null;
let destination = null;
let bridge = null;
let bridgeConnected = false;
let activeStream = null;
let capturing = false;
let bridgeRetryTimer = null;
let bridgeRetryAttempt = 0;

const BRIDGE_RETRY_MAX_ATTEMPTS = 10;
const BRIDGE_RETRY_BASE_MS = 1000;
const BRIDGE_RETRY_CAP_MS = 15000;

function reportState() {
  chrome.runtime.sendMessage({type: 'capture-state', capturing: context !== null, bridgeConnected});
}

function connectBridge() {
  if (!capturing || bridge) return;
  try {
    bridge = new WebSocket('ws://127.0.0.1:17842/v1/tab');
    bridge.binaryType = 'arraybuffer';
    bridge.onopen = () => {
      bridgeRetryAttempt = 0;
      setBridgeConnected(true);
    };
    bridge.onclose = () => {
      bridge = null;
      setBridgeConnected(false);
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
  if (bridgeRetryAttempt >= BRIDGE_RETRY_MAX_ATTEMPTS) return;
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
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === 'get-capture-state') {
    sendResponse({capturing: context !== null, bridgeConnected});
    return false;
  }
  if (message?.type === 'stop-tab-stream') {
    stopCapture()
      .then(() => sendResponse({ok: true}))
      .catch((error) => sendResponse({ok: false, error: String(error)}));
    return true;
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
