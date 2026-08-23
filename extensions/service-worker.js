let capturing = false;

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

async function closeOffscreenIfIdle() {
  if (capturing) return;
  const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
  if (contexts.length > 0) {
    await chrome.offscreen.closeDocument();
  }
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type === 'get-capture-state') {
    sendResponse({capturing});
    return false;
  }
  if (message?.type !== 'capture-active-tab' || !Number.isInteger(message.tabId)) {
    return false;
  }
  (async () => {
    try {
      capturing = true;
      await ensureOffscreenDocument();
      const streamId = await chrome.tabCapture.getMediaStreamId({targetTabId: message.tabId});
      const response = await chrome.runtime.sendMessage({type: 'start-tab-stream', streamId});
      if (!response?.ok) {
        throw new Error(response?.error ?? 'Offscreen stream failed to start');
      }
      sendResponse({ok: true});
    } catch (error) {
      capturing = false;
      await closeOffscreenIfIdle();
      sendResponse({ok: false, error: String(error)});
    }
  })();
  return true;
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type !== 'stop-tab-stream') {
    return false;
  }
  (async () => {
    try {
      if (!capturing) {
        sendResponse({ok: true, capturing});
        return;
      }
      const response = await chrome.runtime.sendMessage({type: 'stop-tab-stream-impl'});
      if (!response?.ok) {
        throw new Error(response?.error ?? 'Offscreen stream failed to stop');
      }
      capturing = false;
      await closeOffscreenIfIdle();
      sendResponse({ok: true, capturing});
    } catch (error) {
      sendResponse({ok: false, capturing, error: String(error)});
    }
  })();
  return true;
});
