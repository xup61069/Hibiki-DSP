let context = null;
let source = null;
let packetizer = null;
let destination = null;
let bridge = null;
let bridgeConnected = false;
let activeStream = null;

function reportState() {
  chrome.runtime.sendMessage({type: 'capture-state', capturing: context !== null, bridgeConnected});
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
    try {
      bridge = new WebSocket('ws://127.0.0.1:17842/v1/tab');
      bridge.binaryType = 'arraybuffer';
      bridge.onopen = () => setBridgeConnected(true);
      bridge.onclose = () => setBridgeConnected(false);
      bridge.onerror = () => setBridgeConnected(false);
      packetizer.port.onmessage = (event) => {
        if (bridge?.readyState === WebSocket.OPEN && event.data instanceof ArrayBuffer) {
          bridge.send(event.data);
        }
      };
    } catch (_) {
      // The optional native bridge may not be running. Local playback remains
      // connected through MediaStreamDestination in that case.
      bridge = null;
    }
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
