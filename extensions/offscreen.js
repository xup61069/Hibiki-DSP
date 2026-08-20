let context = null;
let source = null;
let destination = null;

chrome.runtime.onMessage.addListener(async (message) => {
  if (message?.type !== 'start-tab-stream' || typeof message.streamId !== 'string') return;
  context?.close();
  context = new AudioContext();
  const constraints = {
    audio: {
      mandatory: {chromeMediaSource: 'tab', chromeMediaSourceId: message.streamId}
    },
    video: false
  };
  const stream = await navigator.mediaDevices.getUserMedia(constraints);
  source = context.createMediaStreamSource(stream);
  destination = context.createMediaStreamDestination();
  source.connect(destination);
  await context.resume();
  // The native bridge contract will consume this destination in a later layer.
  // Keeping the graph alive here prevents the tab stream from ending when the
  // popup closes; no microphone or hidden tab is captured.
});
