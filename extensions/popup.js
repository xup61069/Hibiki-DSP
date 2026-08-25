const button = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const status = document.getElementById('status');

let capturing = false;
let bridgeConnected = false;
let bridgeReconnectState = 'idle';
let droppedPackets = 0;

function render() {
  button.disabled = capturing;
  stopButton.disabled = !capturing;
  if (!status.dataset.error) {
    const base = capturing ? reconnectText() : '待機中';
    if (capturing && droppedPackets > 0) {
      status.textContent = base + '（已丢棃 ' + droppedPackets + ' 個封包）';
    } else {
      status.textContent = base;
    }
  }
}

function reconnectText() {
  if (bridgeConnected) return '擷取中－已連接 Hibiki';
  switch (bridgeReconnectState) {
    case 'waiting':
      return '擷取中－等待重試連線';
    case 'retrying':
      return '擷取中－正在重試連線';
    case 'exhausted':
      return '擷取中－已用完有限重試；分頁播放不受影響';
    default:
      return '擷取中－未偵測到本地 bridge';
  }
}

function applyState(state) {
  capturing = state?.capturing === true;
  bridgeConnected = state?.bridgeConnected === true;
  bridgeReconnectState = typeof state?.bridgeReconnectState === 'string'
    ? state.bridgeReconnectState
    : 'idle';
  droppedPackets = typeof state?.droppedPackets === 'number' && Number.isFinite(state.droppedPackets)
    ? Math.max(0, Math.floor(state.droppedPackets))
    : 0;
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
  status.textContent = '要求擷取中…';
  try {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    if (!tab?.id) {
      status.textContent = '未找到使用中的分頁';
      setBusy(false);
      return;
    }
    const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id});
    if (response?.ok) {
      capturing = true;
      render();
    } else {
      status.textContent = response?.error ?? '擷取失敗';
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
  status.textContent = '停止擷取中…';
  try {
    const response = await chrome.runtime.sendMessage({type: 'stop-capture'});
    if (response?.ok) {
      applyState({capturing: false});
    } else {
      status.textContent = response?.error ?? '停止失敗';
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
