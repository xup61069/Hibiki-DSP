let captureLifecycleTail = Promise.resolve();

function enqueueCaptureLifecycle(operation) {
  const next = captureLifecycleTail.then(operation, operation);
  captureLifecycleTail = next.catch(() => {});
  return next;
}

async function ensureOffscreenDocument() {
  const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
  if (contexts.length === 0) {
    await chrome.offscreen.createDocument({
      url: 'offscreen.html',
      reasons: ['USER_MEDIA'],
      justification: 'Keep the user-requested tab audio stream alive while the popup closes.'
    });
  }
}

async function closeOffscreenDocument() {
  await chrome.offscreen.closeDocument();
}

async function startCapture(message) {
  await ensureOffscreenDocument();
  const streamId = await chrome.tabCapture.getMediaStreamId({targetTabId: message.tabId});
  const response = await chrome.runtime.sendMessage({type: 'start-tab-stream', streamId});
  if (!response?.ok) throw new Error(response?.error ?? 'Offscreen failed to start the stream');
}

async function stopCapture() {
  const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
  if (contexts.length === 0) return;
  try {
    await chrome.runtime.sendMessage({type: 'stop-tab-stream'});
  } finally {
    try { await closeOffscreenDocument(); } catch (_) {}
  }
}

async function releaseOffscreenDocumentIfIdle() {
  const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
  if (contexts.length === 0) return;
  try {
    const state = await chrome.runtime.sendMessage({type: 'get-capture-state'});
    if (state?.capturing === true) return;
  } catch (_) {
    // An unresponsive document cannot attest to an active replacement capture.
  }
  try { await closeOffscreenDocument(); } catch (_) {}
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type === 'capture-active-tab' && Number.isInteger(message.tabId)) {
    enqueueCaptureLifecycle(() => startCapture(message))
      .then(() => sendResponse({ok: true}))
      .catch((error) => sendResponse({ok: false, error: String(error)}));
    return true;
  }
  if (message?.type === 'stop-capture') {
    enqueueCaptureLifecycle(() => stopCapture())
      .then(() => sendResponse({ok: true}))
      .catch((error) => sendResponse({ok: false, error: String(error)}));
    return true;
  }
  if (message?.type === 'get-capture-state') {
    (async () => {
      try {
        const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
        if (contexts.length === 0) { sendResponse({capturing: false}); return; }
        const state = await chrome.runtime.sendMessage({type: 'get-capture-state'});
        sendResponse(state);
      } catch (_) {
        sendResponse({capturing: false});
      }
    })();
    return true;
  }
  if (message?.type === 'offscreen-capture-released' && typeof sender.url === 'string' && sender.url.endsWith('/offscreen.html')) {
    enqueueCaptureLifecycle(() => releaseOffscreenDocumentIfIdle())
      .then(() => sendResponse({ok: true}))
      .catch((error) => sendResponse({ok: false, error: String(error)}));
    return true;
  }
  return false;
});
