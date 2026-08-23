let context = null;
let source = null;
let packetizer = null;
let destination = null;
let bridge = null;

function disconnectCaptureGraph() {
  packetizer?.port.close();
  source?.disconnect();
  destination?.disconnect();
  bridge?.close();
  packetizer = null;
  source = null;
  destination = null;
  bridge = null;
}

chrome.runtime.onMessage.addListener((message, sendResponse) => {
  if (message?.type === 'get-capture-state-impl') {
    sendResponse({capturing: context !== null});
    return true;
  }
  if (message?.type !== 'start-tab-stream' || typeof message.streamId !== 'string') return false;
  (async () => {
    try {
      disconnectCaptureGraph();
      await context?.close();
      context = new AudioContext();
      await context.audioWorklet.addModule('audio-worklet.js');
      const constraints = {
        audio: {
          mandatory: {chromeMediaSource: 'tab', chromeMediaSourceId: message.streamId}
        },
        video: false
      };
      const stream = await navigator.mediaDevices.getUserMedia(constraints);
      source = context.createMediaStreamSource(stream);
      packetizer = new AudioWorkletNode(context, 'hibiki-tab-packetizer');
      destination = context.createMediaStreamDestination();
      source.connect(packetizer).connect(destination);
      try {
        bridge = new WebSocket('ws://127.0.0.1:17842/v1/tab');
        bridge.binaryType = 'arraybuffer';
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
      sendResponse({ok: true});
      // Keeping the graph alive here prevents the tab stream from ending when the
      // popup closes; no microphone or hidden tab is captured. The native bridge
      // consumes HIBT packets only when the user has started it separately.
    } catch (error) {
      disconnectCaptureGraph();
      await context?.close();
      context = null;
      sendResponse({ok: false, error: String(error)});
    }
  })();
  return true;
});

chrome.runtime.onMessage.addListener((message, sendResponse) => {
  if (message?.type !== 'stop-tab-stream-impl') return false;
  (async () => {
    try {
      disconnectCaptureGraph();
      await context?.close();
      context = null;
      sendResponse({ok: true});
    } catch (error) {
      sendResponse({ok: false, error: String(error)});
    }
  })();
  return true;
});
