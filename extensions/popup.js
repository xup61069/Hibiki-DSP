const button = document.getElementById('capture');
const status = document.getElementById('status');

button.addEventListener('click', async () => {
  button.disabled = true;
  status.textContent = 'Requesting capture…';
  const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
  if (!tab?.id) {
    status.textContent = 'No active tab';
    button.disabled = false;
    return;
  }
  const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id});
  status.textContent = response?.ok ? 'Capture started' : (response?.error ?? 'Capture failed');
  button.disabled = false;
});
