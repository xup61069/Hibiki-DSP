const button = document.getElementById('capture');
const stopButton = document.getElementById('stop');
const retryButton = document.getElementById('retry-bridge');
const diagnosticsButton = document.getElementById('copy-diagnostics');
const status = document.getElementById('status');

let capturing = false;
let bridgeConnected = false;
let bridgeReconnectState = 'idle';
let droppedPackets = 0;
let retryBusy = false;
let diagnosticsBusy = false;
let diagnosticsTimer = 0;

function render() {
  button.disabled = capturing;
  stopButton.disabled = !capturing;
  retryButton.hidden = !(capturing && !bridgeConnected && bridgeReconnectState === 'exhausted');
  retryButton.disabled = !retryButton.hidden || retryBusy;
  if (!status.dataset.error) {
    status.textContent = capturing ? reconnectText() : '閒置';
  }
}

function droppedPacketsText() {
  const value = Number.isFinite(droppedPackets) && droppedPackets >= 0
    ? Math.floor(droppedPackets)
    : 0;
  return `；已丟棄 ${value} 個 packet`;
}

function reconnectText() {
  if (bridgeConnected) return `擷取中 — Hibiki 已連線${droppedPacketsText()}`;
  switch (bridgeReconnectState) {
    case 'waiting':
      return `擷取中 — Hibiki 將重試連線${droppedPacketsText()}`;
    case 'retrying':
      return `擷取中 — Hibiki 正在重試連線${droppedPacketsText()}`;
    case 'exhausted':
      return `擷取中 — Hibiki 已停止有限次重試；分頁播放繼續${droppedPacketsText()}`;
    default:
      return `擷取中 — 未偵測到 native bridge${droppedPacketsText()}`;
  }
}

function buildDiagnosticsSnapshot() {
  return [
    'Hibiki extension diagnostic snapshot',
    `capturing: ${capturing}`,
    `bridgeConnected: ${bridgeConnected}`,
    `bridgeReconnectState: ${bridgeReconnectState}`,
    `droppedPackets: ${Number.isFinite(droppedPackets) && droppedPackets >= 0 ? Math.floor(droppedPackets) : 0}`,
    `timestampUtc: ${new Date().toISOString()}`,
  ].join('\n');
}

async function writeClipboard(text) {
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(text);
    return true;
  }
  const textarea = document.createElement('textarea');
  textarea.value = text;
  textarea.setAttribute('readonly', '');
  textarea.style.position = 'fixed';
  textarea.style.opacity = '0';
  document.body.appendChild(textarea);
  textarea.select();
  try {
    if (!document.execCommand('copy')) throw new Error('copy-failed');
    return true;
  } finally {
    textarea.remove();
  }
}

function applyState(state) {
  capturing = state?.capturing === true;
  bridgeConnected = state?.bridgeConnected === true;
  bridgeReconnectState = typeof state?.bridgeReconnectState === 'string'
    ? state.bridgeReconnectState
    : 'idle';
  droppedPackets = state?.droppedPackets ?? droppedPackets;
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
  status.textContent = '正在要求擷取…';
  try {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    if (!tab?.id) {
      status.textContent = '沒有使用中的分頁';
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
  status.textContent = '正在停止擷取…';
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

retryButton.addEventListener('click', async () => {
  if (retryButton.hidden || retryBusy) return;
  delete status.dataset.error;
  retryBusy = true;
  render();
  status.textContent = '正在重新連線…';
  try {
    const response = await chrome.runtime.sendMessage({type: 'retry-tab-bridge'});
    if (response?.ok) {
      bridgeReconnectState = 'retrying';
      status.textContent = '已要求重新連線；等待 Hibiki 回應';
    } else {
      status.textContent = response?.error === 'bridge-retry-unavailable'
        ? '目前無法重新連線'
        : response?.error ?? '重新連線失敗';
      status.dataset.error = 'true';
    }
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : String(error);
    status.dataset.error = 'true';
  }
  retryBusy = false;
  await refreshState();
});

diagnosticsButton.addEventListener('click', async () => {
  if (diagnosticsBusy) return;
  delete status.dataset.error;
  clearTimeout(diagnosticsTimer);
  diagnosticsBusy = true;
  diagnosticsButton.disabled = true;
  try {
    await refreshState();
    await writeClipboard(buildDiagnosticsSnapshot());
    status.textContent = '已複製匿名診斷快照';
  } catch (error) {
    status.textContent = error instanceof Error ? error.message : String(error);
    status.dataset.error = 'true';
  } finally {
    diagnosticsBusy = false;
    diagnosticsButton.disabled = false;
  }
  diagnosticsTimer = setTimeout(() => {
    if (!status.dataset.error) render();
  }, 2000);
});

refreshState();

document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible') {
    void refreshState();
  }
});
