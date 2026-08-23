const button = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const status = document.getElementById('status');

let capturing = false;
let bridgeConnected = false;

function render() {
  button.disabled = capturing;
  stopButton.disabled = !capturing;
  if (!status.dataset.error) {
    status.textContent = capturing
      ? (bridgeConnected ? 'Capturing — Hibiki connected' : 'Capturing — native bridge not detected')
      : 'Idle';
  }
}

function setBusy(isBusy) {
  button.disabled = isBusy || capturing;
  stopButton.disabled = isBusy || !capturing;
}

async function refreshState() {
  setBusy(true);
  try {
    const state = await chrome.runtime.sendMessage({type: 'get-capture-state'});
    capturing = state?.capturing === true;
    bridgeConnected = state?.bridgeConnected === true;
  } catch (_) {
    capturing = false;
    bridgeConnected = false;
  }
  render();
  setBusy(false);
}

chrome.runtime.onMessage.addListener((message) => {
  if (message?.type !== 'capture-state') return;
  capturing = message.capturing === true;
  bridgeConnected = message.bridgeConnected === true;
  render();
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
    capturing = false;
    bridgeConnected = false;
    render();
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
      capturing = false;
      bridgeConnected = false;
      render();
    } else {
      status.textContent = response?.error ?? 'Stop failed';
      status.dataset.error = 'true';
    }
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : String(error);
    status.dataset.error = 'true';
    capturing = false;
    bridgeConnected = false;
    render();
    await refreshState();
    return;
  }
  setBusy(false);
});

refreshState();
