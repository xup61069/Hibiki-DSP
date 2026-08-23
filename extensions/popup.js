const button = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const status = document.getElementById('status');

function setBusy(isBusy) {
  button.disabled = isBusy;
  stopButton.disabled = isBusy;
}

async function refreshState() {
  setBusy(true);
  try {
    const state = await chrome.runtime.sendMessage({type: 'get-capture-state'});
    updateControls(state?.capturing === true);
  } catch (_) {
    updateControls(false);
  }
  setBusy(false);
}

function updateControls(capturing) {
  button.disabled = capturing;
  stopButton.disabled = !capturing;
  status.textContent = capturing ? 'Capturing' : 'Idle';
}

button.addEventListener('click', async () => {
  setBusy(true);
  status.textContent = 'Requesting capture…';
  const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
  if (!tab?.id) {
    status.textContent = 'No active tab';
    setBusy(false);
    return;
  }
  const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id});
  if (response?.ok) {
    updateControls(true);
  } else {
    status.textContent = response?.error ?? 'Capture failed';
    updateControls(false);
  }
});

stopButton.addEventListener('click', async () => {
  setBusy(true);
  status.textContent = 'Stopping capture…';
  const response = await chrome.runtime.sendMessage({type: 'stop-capture'});
  if (response?.ok) {
    updateControls(false);
  } else {
    status.textContent = response?.error ?? 'Stop failed';
    updateControls(true);
  }
});

refreshState();
