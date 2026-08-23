const captureButton = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const status = document.getElementById('status');

function setState(capturing, message) {
  status.textContent = message;
  stopButton.disabled = !capturing;
}

async function refreshCaptureState() {
  const response = await chrome.runtime.sendMessage({type: 'get-capture-state'});
  if (response?.capturing) {
    setState(true, 'Capturing');
  } else {
    setState(false, response?.error ?? 'Idle');
  }
}

captureButton.addEventListener('click', async () => {
  captureButton.disabled = true;
  setState(false, 'Requesting capture…');
  try {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    if (!tab?.id) {
      setState(false, 'No active tab');
      return;
    }
    const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id});
    if (response?.ok) {
      setState(true, 'Capturing');
    } else {
      setState(false, response?.error ?? 'Capture failed');
    }
  } catch (error) {
    setState(false, String(error));
  } finally {
    captureButton.disabled = false;
  }
});

stopButton.addEventListener('click', async () => {
  stopButton.disabled = true;
  try {
    const response = await chrome.runtime.sendMessage({type: 'stop-tab-stream'});
    if (response?.ok) {
      setState(false, 'Stopped');
    } else {
      setState(response?.capturing === true, response?.error ?? 'Stop failed');
    }
  } catch (error) {
    setState(false, String(error));
  }
});

refreshCaptureState();
