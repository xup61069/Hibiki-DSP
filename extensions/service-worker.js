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

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type === 'capture-active-tab' && Number.isInteger(message.tabId)) {
    (async () => {
      try {
        await ensureOffscreenDocument();
        const streamId = await chrome.tabCapture.getMediaStreamId({targetTabId: message.tabId});
        const response = await chrome.runtime.sendMessage({type: 'start-tab-stream', streamId});
        if (!response?.ok) throw new Error(response?.error ?? 'Offscreen failed to start the stream');
        sendResponse({ok: true});
      } catch (error) {
        sendResponse({ok: false, error: String(error)});
      }
    })();
    return true;
  }
  if (message?.type === 'stop-capture') {
    (async () => {
      try {
        await chrome.runtime.sendMessage({type: 'stop-tab-stream'});
        await closeOffscreenDocument();
        sendResponse({ok: true});
      } catch (error) {
        try { await closeOffscreenDocument(); } catch (_) {}
        sendResponse({ok: false, error: String(error)});
      }
    })();
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
    (async () => {
      try {
        const contexts = await chrome.runtime.getContexts({contextTypes: ['OFFSCREEN_DOCUMENT']});
        if (contexts.length > 0) await closeOffscreenDocument();
      } catch (_) {
        // Already closed or closing; the next state query reports the truth.
      }
    })();
    return false;
  }
  return false;
});
