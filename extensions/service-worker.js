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

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type !== 'capture-active-tab' || !Number.isInteger(message.tabId)) {
    return false;
  }
  (async () => {
    try {
      await ensureOffscreenDocument();
      const streamId = await chrome.tabCapture.getMediaStreamId({targetTabId: message.tabId});
      await chrome.runtime.sendMessage({type: 'start-tab-stream', streamId});
      sendResponse({ok: true});
    } catch (error) {
      sendResponse({ok: false, error: String(error)});
    }
  })();
  return true;
});
