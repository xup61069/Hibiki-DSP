const button = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const status = document.getElementById('status');

let capturing = false;
let bridgeConnected = false;
let bridgeReconnectState = 'idle';

function render() {
  button.disabled = capturing;
  stopButton.disabled = !capturing;
  if (!status.dataset.error) {
    status.textContent = capturing ? reconnectText() : 'Idle';
  }
}

function reconnectText() {
  if (bridgeConnected) return 'Capturing — Hibiki connected';
  switch (bridgeReconnectState) {
    case 'waiting':
      return 'Capturing — Hibiki will retry connection';
    case 'retrying':
      return 'Capturing — Hibiki is retrying connection';
    case 'exhausted':
      return 'Capturing — Hibiki stopped after bounded retries; tab playback continues';
    default:
      return 'Capturing — native bridge not detected';
  }
}

function applyState(state) {
  capturing = state?.capturing === true;
  bridgeConnected = state?.bridgeConnected === true;
  bridgeReconnectState = typeof state?.bridgeReconnectState === 'string'
    ? state.bridgeReconnectState
    : 'idle';
  render();
}

function setBusy(isBusy) {
  button.disabled = isBusy || capturing;
  stopButton.disabled = isBusy || !capturing;
}

async function refreshState() {
  setBusy(true);
  try {
    applyState(await chrome.runtime.sendMessage({type: 'get-capture-state'}));
  } catch (_) {
    applyState({capturing: false});
  }
  setBusy(false);
}

chrome.runtime.onMessage.addListener((message) => {
  if (message?.type !== 'capture-state') return;
  applyState(message);
});

button.addEventListener('click', async () => {
  delete status.dataset.error;
  setBusy(true);
  status.textContent = 'Requesting capture…';
  try {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    if (!tab?.id) {
      status.textContent = 'No active tab';
      setBusy(false);
      return;
    }
    const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id});
    if (response?.ok) {
      capturing = true;
      render();
    } else {
      status.textContent = response?.error ?? 'Capture failed';
      status.dataset.error = 'true';
      capturing = false;
      render();
    }
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : String(error);
    status.dataset.error = 'true';
    applyState({capturing: false});
    await refreshState();
    return;
  }
  setBusy(false);
});

stopButton.addEventListener('click', async () => {
  delete status.dataset.error;
  setBusy(true);
  status.textContent = 'Stopping capture…';
  try {
    const response = await chrome.runtime.sendMessage({type: 'stop-capture'});
    if (response?.ok) {
      applyState({capturing: false});
    } else {
      status.textContent = response?.error ?? 'Stop failed';
      status.dataset.error = 'true';
    }
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : String(error);
    status.dataset.error = 'true';
    applyState({capturing: false});
    await refreshState();
    return;
  }
  setBusy(false);
});

refreshState();

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    void refreshState();
  }
});
