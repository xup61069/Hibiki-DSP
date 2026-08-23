[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$allowedPermissions = [System.Collections.Generic.HashSet[string]]::new(
  [string[]]@('activeTab', 'tabCapture', 'offscreen'),
  [System.StringComparer]::Ordinal
)
$requiredPermissions = @('activeTab', 'tabCapture', 'offscreen')
$allowedHostPermissions = [System.Collections.Generic.HashSet[string]]::new(
  [string[]]@('http://127.0.0.1/*'),
  [System.StringComparer]::Ordinal
)
$requiredHostPermissions = @('http://127.0.0.1/*')

function Get-CspDirectiveSources([string]$csp, [string]$directive, [string]$sourceName) {
  $matches = @()
  foreach ($segment in ($csp -split ';')) {
    $tokens = @($segment.Trim() -split '\s+' | Where-Object { $_ })
    if ($tokens.Count -eq 0 -or $tokens[0] -ine $directive) { continue }
    $matches += ,@($tokens | Select-Object -Skip 1)
  }
  if ($matches.Count -ne 1) {
    throw "CSP must contain exactly one $directive directive in $sourceName."
  }
  return ,@($matches[0])
}

function Assert-ExactCspDirectiveSet([string]$csp, [string]$sourceName) {
  $allowedDirectives = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@('script-src', 'object-src', 'connect-src'),
    [System.StringComparer]::OrdinalIgnoreCase
  )
  $seenDirectives = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
  )
  foreach ($segment in ($csp -split ';')) {
    $tokens = @($segment.Trim() -split '\s+' | Where-Object { $_ })
    if ($tokens.Count -eq 0) {
      throw "CSP contains an empty directive in $sourceName."
    }
    $directive = [string]$tokens[0]
    if (-not $allowedDirectives.Contains($directive)) {
      throw "CSP directive '$directive' is not allowed in $sourceName."
    }
    if (-not $seenDirectives.Add($directive)) {
      throw "CSP contains duplicate directive '$directive' in $sourceName."
    }
  }
  if ($seenDirectives.Count -ne $allowedDirectives.Count) {
    throw "CSP must contain exactly the script-src, object-src, and connect-src directives in $sourceName."
  }
}

function Assert-ExtensionSourcePolicy(
  [string]$popupSource,
  [string]$serviceWorkerSource,
  [string]$offscreenSource,
  [string]$workletSource,
  [string]$sourceName
) {
  if ($popupSource -notmatch 'button\.addEventListener\(\s*(?:\x27|\x22)click(?:\x27|\x22)') {
    throw "Popup must delegate capture from its click handler in $sourceName."
  }
  if ($popupSource -notmatch 'chrome\.runtime\.sendMessage\(\s*\{\s*type\s*:\s*(?:\x27|\x22)capture-active-tab') {
    throw "Popup must send the capture-active-tab request in $sourceName."
  }
  if ($popupSource -notmatch 'chrome\.runtime\.sendMessage\(\s*\{\s*type\s*:\s*(?:\x27|\x22)stop-capture') {
    throw "Popup must send the stop-capture request in $sourceName."
  }
  if ($popupSource -notmatch 'chrome\.runtime\.sendMessage\(\s*\{\s*type\s*:\s*(?:\x27|\x22)get-capture-state') {
    throw "Popup must query get-capture-state on open in $sourceName."
  }
  if ($popupSource -notmatch 'capture-state') {
    throw "Popup must listen for live capture-state messages in $sourceName."
  }
  if ($popupSource -notmatch 'response\?\.ok') {
    throw "Popup start/stop handlers must test response?.ok before claiming success in $sourceName."
  }
  if ($popupSource -notmatch 'response\?\.(?:error|ok)\s*\?\?') {
    throw "Popup must surface real response errors with an honest fallback message in $sourceName."
  }
  if ($popupSource -notmatch 'status\.dataset\.error') {
    throw "Popup must persist error text via status.dataset.error so refreshState cannot overwrite it with Idle in $sourceName."
  }
  if ($popupSource -match 'chrome\.offscreen\.') {
    throw "Popup must not access offscreen directly in $sourceName."
  }
  if ($popupSource -match 'chrome\.tabCapture\.') {
    throw "Popup must not access tabCapture directly in $sourceName."
  }
  if ($offscreenSource -match 'chrome\.tabCapture\.' -or $workletSource -match 'chrome\.tabCapture\.') {
    throw "Only the service worker may access tabCapture in $sourceName."
  }

  foreach ($pattern in @(
      'chrome\.runtime\.onMessage\.addListener',
      'chrome\.offscreen\.createDocument',
      'chrome\.offscreen\.closeDocument',
      'chrome\.tabCapture\.getMediaStreamId',
      'targetTabId\s*:\s*message\.tabId',
      'stop-capture',
      'get-capture-state'
    )) {
    if ($serviceWorkerSource -notmatch $pattern) {
      throw "Service worker is missing required source boundary '$pattern' in $sourceName."
    }
  }
  if ($serviceWorkerSource -notmatch 'return\s+true\s*;') {
    throw "Service worker onMessage listener must return true to keep the async response channel open in $sourceName."
  }
  if ($serviceWorkerSource -notmatch 'message\?\.type\s*===\s*(?:\x27|\x22)offscreen-capture-released(?:\x27|\x22)') {
    throw "Service worker must handle the offscreen natural-end release notification in $sourceName."
  }
  if ($serviceWorkerSource -notmatch 'typeof\s+sender\.url\s*===\s*(?:\x27|\x22)string(?:\x27|\x22)') {
    throw "Service worker must validate sender URL type before trusting the release notification in $sourceName."
  }
  if ($serviceWorkerSource -notmatch 'sender\.url\.endsWith\(\s*(?:\x27|\x22)/offscreen\.html(?:\x27|\x22)\s*\)') {
    throw "Service worker must accept the natural-end release notification only from offscreen.html in $sourceName."
  }
  if ($serviceWorkerSource -notmatch 'offscreen-capture-released[\s\S]{0,500}closeOffscreenDocument\s*\(') {
    throw "Service worker must close the offscreen document after a validated natural-end release notification in $sourceName."
  }

  foreach ($pattern in @(
      'chrome\.runtime\.onMessage\.addListener',
      'new\s+AudioContext',
      'audio-worklet\.js',
      'new\s+AudioWorkletNode',
      'navigator\.mediaDevices\.getUserMedia',
      'chromeMediaSource\s*:\s*(?:\x27|\x22)tab(?:\x27|\x22)',
      'chromeMediaSourceId\s*:\s*message\.streamId',
      'video\s*:\s*false',
      'sendResponse\s*\(',
      'stop-tab-stream',
      'get-capture-state',
      'bridgeConnected',
      'onopen\s*=',
      'onclose\s*=',
      'activeStream\s*=\s*stream',
      'track\.stop\(\)',
      'track\.addEventListener\(\s*(?:\x27|\x22)ended(?:\x27|\x22)',
      'handleSourceEnded',
      'offscreen-capture-released'
    )) {
    if ($offscreenSource -notmatch $pattern) {
      throw "Offscreen source is missing required source boundary '$pattern' in $sourceName."
    }
  }
  if ($offscreenSource -notmatch 'return\s+true\s*;') {
    throw "Offscreen onMessage listener must return true to keep the async response channel open in $sourceName."
  }
  if ($offscreenSource -notmatch 'async\s+function\s+handleSourceEnded\s*\(\s*\)\s*\{[\s\S]{0,400}await\s+teardownCaptureGraph\s*\(\s*\)\s*;[\s\S]{0,300}offscreen-capture-released') {
    throw "Offscreen natural-end handler must tear down the capture graph before reporting and requesting document release in $sourceName."
  }
  $webSocketMatches = [regex]::Matches(
    $offscreenSource,
    'new\s+WebSocket\s*\(\s*(?:\x27|\x22)(?<url>[^\x27\x22\)]+)(?:\x27|\x22)'
  )
  if ($webSocketMatches.Count -ne 1 -or
      $webSocketMatches[0].Groups['url'].Value -cne 'ws://127.0.0.1:17842/v1/tab') {
    throw "Offscreen source must use exactly the fixed loopback bridge in $sourceName."
  }

  foreach ($pattern in @(
      'registerProcessor\s*\(\s*(?:\x27|\x22)hibiki-tab-packetizer',
      'new\s+ArrayBuffer\s*\(\s*16\s*\+',
      'setUint8\s*\(\s*0\s*,\s*0x48',
      'setUint8\s*\(\s*1\s*,\s*0x49',
      'setUint8\s*\(\s*2\s*,\s*0x42',
      'setUint8\s*\(\s*3\s*,\s*0x54',
      'setUint16\s*\(\s*4\s*,\s*1\s*,\s*true',
      'this\.port\.postMessage'
    )) {
    if ($workletSource -notmatch $pattern) {
      throw "AudioWorklet source is missing required HIBT boundary '$pattern' in $sourceName."
    }
  }
}

function Assert-ExtensionManifestPolicy($manifest, [string]$sourceName) {
  if ($null -eq $manifest) {
    throw "Extension manifest cannot be null in $sourceName"
  }
  if ($manifest.manifest_version -ne 3) {
    throw "Extension manifest in $sourceName must use MV3 (manifest_version 3)."
  }

  $perms = @($manifest.permissions)
  foreach ($req in $script:requiredPermissions) {
    if ($perms -notcontains $req) {
      throw "Missing required permission '$req' in $sourceName."
    }
  }
  foreach ($p in $perms) {
    if (-not $script:allowedPermissions.Contains($p)) {
      throw "Disallowed or broad permission '$p' in $sourceName."
    }
  }

  $hostPerms = @($manifest.host_permissions)
  foreach ($reqHost in $script:requiredHostPermissions) {
    if ($hostPerms -notcontains $reqHost) {
      throw "Missing required host permission '$reqHost' in $sourceName."
    }
  }
  foreach ($hp in $hostPerms) {
    if (-not $script:allowedHostPermissions.Contains($hp)) {
      throw "Disallowed or broad host permission '$hp' in $sourceName."
    }
    if ($hp -eq '<all_urls>' -or $hp -match '^(https?|wss?|\*):\/\/\*') {
      throw "Wildcard host permission '$hp' is forbidden in $sourceName."
    }
  }

  if ($null -eq $manifest.content_security_policy -or
      [string]::IsNullOrWhiteSpace($manifest.content_security_policy.extension_pages)) {
    throw "Missing content_security_policy.extension_pages in $sourceName."
  }

  $csp = [string]$manifest.content_security_policy.extension_pages
  Assert-ExactCspDirectiveSet $csp $sourceName
  if ($csp -match 'unsafe-eval') {
    throw "CSP contains unsafe-eval in $sourceName."
  }
  if ($csp -match 'unsafe-inline') {
    throw "CSP contains unsafe-inline in $sourceName."
  }
  $scriptSources = @(Get-CspDirectiveSources $csp 'script-src' $sourceName)
  if ($scriptSources.Count -ne 1 -or $scriptSources[0] -cne "'self'") {
    throw "CSP script-src must contain only 'self' in $sourceName."
  }
  $objectSources = @(Get-CspDirectiveSources $csp 'object-src' $sourceName)
  if ($objectSources.Count -ne 1 -or $objectSources[0] -cnotin @("'self'", "'none'")) {
    throw "CSP object-src must contain only 'self' or 'none' in $sourceName."
  }
  $connectSources = @(Get-CspDirectiveSources $csp 'connect-src' $sourceName)
  if ($connectSources.Count -ne 1 -or $connectSources[0] -cne 'ws://127.0.0.1:17842') {
    throw "CSP connect-src must contain only ws://127.0.0.1:17842 without wildcard origins in $sourceName."
  }
}

if ($SelfTest) {
  $validJson = '{"manifest_version": 3, "permissions": ["activeTab", "tabCapture", "offscreen"], "host_permissions": ["http://127.0.0.1/*"], "content_security_policy": {"extension_pages": "script-src ''self''; object-src ''self''; connect-src ws://127.0.0.1:17842"}}'

  $valid = $validJson | ConvertFrom-Json
  Assert-ExtensionManifestPolicy $valid 'selftest-valid'

  $missingPerm = $validJson | ConvertFrom-Json
  $missingPerm.permissions = @('activeTab', 'offscreen')
  $caught = $false
  try { Assert-ExtensionManifestPolicy $missingPerm 'selftest-missing-perm' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing permission failure.' }

  $extraPerm = $validJson | ConvertFrom-Json
  $extraPerm.permissions = @('activeTab', 'tabCapture', 'offscreen', 'webRequest')
  $caught = $false
  try { Assert-ExtensionManifestPolicy $extraPerm 'selftest-extra-perm' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected extra permission failure.' }

  $wildcardHost = $validJson | ConvertFrom-Json
  $wildcardHost.host_permissions = @('http://127.0.0.1/*', 'https://*/*')
  $caught = $false
  try { Assert-ExtensionManifestPolicy $wildcardHost 'selftest-wildcard-host' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected wildcard host failure.' }

  $unsafeCsp = $validJson | ConvertFrom-Json
  $unsafeCsp.content_security_policy.extension_pages = "script-src 'self' 'unsafe-eval'; object-src 'self'; connect-src ws://127.0.0.1:17842"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $unsafeCsp 'selftest-unsafe-csp' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected unsafe CSP failure.' }

  $externalConnect = $validJson | ConvertFrom-Json
  $externalConnect.content_security_policy.extension_pages = "script-src 'self'; object-src 'self'; connect-src ws://127.0.0.1:17842 https://example.com"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $externalConnect 'selftest-external-connect' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected external connect-src failure.' }

  $externalScript = $validJson | ConvertFrom-Json
  $externalScript.content_security_policy.extension_pages = "script-src 'self' https://cdn.example; object-src 'self'; connect-src ws://127.0.0.1:17842"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $externalScript 'selftest-external-script' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected external script-src failure.' }

  $externalObject = $validJson | ConvertFrom-Json
  $externalObject.content_security_policy.extension_pages = "script-src 'self'; object-src 'self' https://cdn.example; connect-src ws://127.0.0.1:17842"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $externalObject 'selftest-external-object' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected external object-src failure.' }

  $missingCsp = $validJson | ConvertFrom-Json
  $missingCsp.content_security_policy = [pscustomobject]@{}
  $caught = $false
  try { Assert-ExtensionManifestPolicy $missingCsp 'selftest-missing-csp' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing extension_pages CSP failure.' }

  $invalidObjectSource = $validJson | ConvertFrom-Json
  $invalidObjectSource.content_security_policy.extension_pages = "script-src 'self'; object-src 'script'; connect-src ws://127.0.0.1:17842"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $invalidObjectSource 'selftest-invalid-object-src' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected invalid object-src failure.' }

  $wrongLoopback = $validJson | ConvertFrom-Json
  $wrongLoopback.content_security_policy.extension_pages = "script-src 'self'; object-src 'self'; connect-src ws://127.0.0.1:17843"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $wrongLoopback 'selftest-wrong-loopback' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected wrong loopback endpoint failure.' }

  $extraDirective = $validJson | ConvertFrom-Json
  $extraDirective.content_security_policy.extension_pages = "script-src 'self'; object-src 'self'; connect-src ws://127.0.0.1:17842; default-src *"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $extraDirective 'selftest-extra-directive' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected an extra CSP directive failure.' }

  $duplicateDirective = $validJson | ConvertFrom-Json
  $duplicateDirective.content_security_policy.extension_pages = "script-src 'self'; object-src 'self'; connect-src ws://127.0.0.1:17842; script-src 'self'"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $duplicateDirective 'selftest-duplicate-directive' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected a duplicate CSP directive failure.' }

  $emptyDirective = $validJson | ConvertFrom-Json
  $emptyDirective.content_security_policy.extension_pages = "script-src 'self'; ; object-src 'self'; connect-src ws://127.0.0.1:17842"
  $caught = $false
  try { Assert-ExtensionManifestPolicy $emptyDirective 'selftest-empty-directive' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected an empty CSP directive failure.' }

  $sourceFixture = @{
    popup = "button.addEventListener('click', async () => { delete status.dataset.error; const response = await chrome.runtime.sendMessage({type: 'capture-active-tab', tabId: tab.id}); if (response?.ok) { render(); } else { status.textContent = response?.error ?? 'Capture failed'; status.dataset.error = 'true'; render(); } }); stopButton.addEventListener('click', async () => { delete status.dataset.error; const response = await chrome.runtime.sendMessage({type: 'stop-capture'}); if (response?.ok) { render(); } else { status.textContent = response?.error ?? 'Stop failed'; status.dataset.error = 'true'; } }); chrome.runtime.onMessage.addListener((message) => { if (message.type === 'capture-state') render(); }); refreshState(); async function refreshState() { await chrome.runtime.sendMessage({type: 'get-capture-state'}); }"
    serviceWorker = "chrome.runtime.onMessage.addListener((message, sender, sendResponse) => { if (message.type === 'stop-capture') { (async () => { await closeOffscreenDocument(); await chrome.offscreen.closeDocument(); sendResponse({stopped: true}); })(); return true; } if (message?.type === 'offscreen-capture-released' && typeof sender.url === 'string' && sender.url.endsWith('/offscreen.html')) { closeOffscreenDocument(); return false; } if (message.type === 'get-capture-state') { (async () => { const state = await chrome.runtime.sendMessage({type: 'get-capture-state'}); sendResponse(state); })(); return true; } if (message.type === 'start-capture') { (async () => { await chrome.offscreen.createDocument({url: 'offscreen.html'}); const streamId = await chrome.tabCapture.getMediaStreamId({targetTabId: message.tabId}); sendResponse({streamId}); })(); return true; } return false; });"
    offscreen = "chrome.runtime.onMessage.addListener(async (message, _sender, sendResponse) => { if (message.type === 'stop-tab-stream') { activeStream.getTracks().forEach(track => track.stop()); sendResponse({stopped: true}); return true; } if (message.type === 'get-capture-state') { sendResponse({capturing: true, bridgeConnected: false}); return false; } if (message.type !== 'start-tab-stream') return false; const context = new AudioContext(); await context.audioWorklet.addModule('audio-worklet.js'); const node = new AudioWorkletNode(context, 'hibiki-tab-packetizer'); const constraints = {audio: {mandatory: {chromeMediaSource: 'tab', chromeMediaSourceId: message.streamId}}, video: false}; await navigator.mediaDevices.getUserMedia(constraints); const stream = await navigator.mediaDevices.getUserMedia(constraints); activeStream = stream; track.addEventListener('ended', handleSourceEnded); sendResponse({ok: true}); const bridge = new WebSocket('ws://127.0.0.1:17842/v1/tab'); bridgeConnected = true; bridge.onopen = () => {}; bridge.onclose = () => {}; return true; }); async function handleSourceEnded() { await teardownCaptureGraph(); reportState(); chrome.runtime.sendMessage({type: 'offscreen-capture-released'}); }"
    worklet = "const packet = new ArrayBuffer(16 + 4); const view = new DataView(packet); view.setUint8(0, 0x48); view.setUint8(1, 0x49); view.setUint8(2, 0x42); view.setUint8(3, 0x54); view.setUint16(4, 1, true); this.port.postMessage(packet, [packet]); registerProcessor('hibiki-tab-packetizer', HibikiTabPacketizer);"
  }
  Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-source-valid'

  $missingStopHandler = $sourceFixture.popup -replace "chrome\.runtime\.sendMessage\(\{type: 'stop-capture'\}\)", 'console.log()'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $missingStopHandler $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-stop-handler' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing popup stop handler failure.' }

  $missingStateQuery = $sourceFixture.popup -replace "chrome\.runtime\.sendMessage\(\{type: 'get-capture-state'\}\)", 'console.log()'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $missingStateQuery $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-state-query' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing popup state query failure.' }

  $missingWorkerStop = $sourceFixture.serviceWorker -replace "message\.type === 'stop-capture'", "message.type === 'other'"
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $missingWorkerStop $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-worker-stop' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing service-worker stop boundary failure.' }

  $missingWorkerReturnTrue = $sourceFixture.serviceWorker -replace 'return true;', 'return false;'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $missingWorkerReturnTrue $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-worker-return-true' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing service-worker async return-true failure.' }

  $missingOffscreenStop = $sourceFixture.offscreen -replace 'track\.stop\(\)', 'console.log()'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingOffscreenStop $sourceFixture.worklet 'selftest-missing-offscreen-stop' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing offscreen stream-stop failure.' }

  $missingBridgeState = $sourceFixture.offscreen -replace 'bridgeConnected', 'removed'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingBridgeState $sourceFixture.worklet 'selftest-missing-bridge-state' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing bridge state failure.' }

  $missingPopupStateListener = $sourceFixture.popup -replace 'capture-state', 'other-state'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $missingPopupStateListener $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-popup-state-listener' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing popup state listener failure.' }

  $missingStartResponse = $sourceFixture.offscreen -replace 'sendResponse\(\{[^}]*\}\)', 'console.log()'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingStartResponse $sourceFixture.worklet 'selftest-missing-start-response' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing offscreen start response failure.' }

  $missingAsyncReturnTrue = $sourceFixture.offscreen -replace 'return true;', 'return false;'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingAsyncReturnTrue $sourceFixture.worklet 'selftest-missing-async-return-true' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing offscreen async return-true failure.' }

  $bareAsyncReturn = $sourceFixture.offscreen -replace 'return true;', 'asyncHandled = true;'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $bareAsyncReturn $sourceFixture.worklet 'selftest-bare-async-return' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected non-statement async return failure.' }
  $popupDirectCapture = $sourceFixture.popup + " chrome.tabCapture.getMediaStreamId({targetTabId: tab.id});"
  $caught = $false
  try { Assert-ExtensionSourcePolicy $popupDirectCapture $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-popup-direct-capture' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected direct popup tabCapture failure.' }

  $missingWorkerOwnership = $sourceFixture.serviceWorker -replace 'chrome\.offscreen\.createDocument', 'chrome.runtime.getPlatformInfo'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $missingWorkerOwnership $sourceFixture.offscreen $sourceFixture.worklet 'selftest-worker-ownership' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing service-worker ownership failure.' }

  $externalBridge = $sourceFixture.offscreen -replace 'ws://127\.0\.0\.1:17842/v1/tab', 'wss://example.com/tab'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $externalBridge $sourceFixture.worklet 'selftest-external-bridge' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected external bridge failure.' }

  $offscreenDirectCapture = $sourceFixture.offscreen + " chrome.tabCapture.getMediaStreamId({targetTabId: 1});"
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $offscreenDirectCapture $sourceFixture.worklet 'selftest-offscreen-direct-capture' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected offscreen direct tabCapture failure.' }

  $wrongMediaSource = $sourceFixture.offscreen -replace "chromeMediaSource: 'tab'", "chromeMediaSource: 'microphone'"
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $wrongMediaSource $sourceFixture.worklet 'selftest-microphone-source' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected non-tab media source failure.' }

  $missingStreamId = $sourceFixture.offscreen -replace 'chromeMediaSourceId: message\.streamId', "deviceId: 'default'"
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingStreamId $sourceFixture.worklet 'selftest-device-source' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected device media source failure.' }

  $videoCapture = $sourceFixture.offscreen -replace 'video: false', 'video: true'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $videoCapture $sourceFixture.worklet 'selftest-video-source' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected video capture failure.' }

  $missingPacketizer = $sourceFixture.worklet -replace 'registerProcessor\(\x27hibiki-tab-packetizer\x27, HibikiTabPacketizer\);', ''
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $sourceFixture.offscreen $missingPacketizer 'selftest-missing-packetizer' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing packetizer failure.' }

  $multilineCsp = $validJson | ConvertFrom-Json
  $multilineCsp.content_security_policy.extension_pages = @"
script-src    'self' ;
object-src    'none' ;
connect-src   ws://127.0.0.1:17842
"@
  Assert-ExtensionManifestPolicy $multilineCsp 'selftest-multiline-csp'

  $droppedResponseCheck = $sourceFixture.popup -replace "if \(response\?\.ok\)", 'if (true)'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $droppedResponseCheck $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-popup-drops-ok-check' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected popup dropped ok-check failure.' }

  $droppedEndedListener = $sourceFixture.offscreen -replace "track\.addEventListener\('ended', handleSourceEnded\);", ''
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $droppedEndedListener $sourceFixture.worklet 'selftest-offscreen-drops-ended-listener' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing offscreen ended listener failure.' }

  $droppedReleaseNotification = $sourceFixture.offscreen -replace "sendMessage\(\{type: 'offscreen-capture-released'\}\);", ''
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $droppedReleaseNotification $sourceFixture.worklet 'selftest-offscreen-drops-release-notification' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing natural-end release notification failure.' }

  $missingHandlerDefinition = $sourceFixture.offscreen -replace 'async function handleSourceEnded\(\) \{.*\}', ''
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $sourceFixture.serviceWorker $missingHandlerDefinition $sourceFixture.worklet 'selftest-offscreen-missing-ended-handler-definition' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing natural-end handler definition failure.' }

  $unvalidatedSenderUrlType = $sourceFixture.serviceWorker -replace "typeof sender\.url === 'string'", 'false'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $unvalidatedSenderUrlType $sourceFixture.offscreen $sourceFixture.worklet 'selftest-release-skips-sender-type-check' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected unvalidated release sender type failure.' }

  $unvalidatedSenderPath = $sourceFixture.serviceWorker -replace "sender\.url\.endsWith\('/offscreen.html'\)", 'true'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $unvalidatedSenderPath $sourceFixture.offscreen $sourceFixture.worklet 'selftest-release-skips-sender-path-check' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected unvalidated release sender path failure.' }

  $releaseWithoutClose = $sourceFixture.serviceWorker -replace 'closeOffscreenDocument\(\); return false;', 'return false;'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $sourceFixture.popup $releaseWithoutClose $sourceFixture.offscreen $sourceFixture.worklet 'selftest-release-without-close' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected release notification without document close failure.' }

  $missingErrorPersistence = $sourceFixture.popup -replace 'status\.dataset\.error', 'status.dataset.ok'
  $caught = $false
  try { Assert-ExtensionSourcePolicy $missingErrorPersistence $sourceFixture.serviceWorker $sourceFixture.offscreen $sourceFixture.worklet 'selftest-missing-error-persistence' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing error persistence failure.' }

  Write-Output 'Browser extension policy self-test passed (42 cases).'
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repo 'extensions/manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

Assert-ExtensionManifestPolicy $manifest 'extensions/manifest.json'

foreach ($path in @(
  'extensions/service-worker.js',
  'extensions/popup.html',
  'extensions/popup.js',
  'extensions/offscreen.html',
  'extensions/offscreen.js',
  'extensions/audio-worklet.js'
)) {
  if (-not (Test-Path (Join-Path $repo $path))) {
    throw "Missing extension source: $path"
  }
}

$popupSource = Get-Content -LiteralPath (Join-Path $repo 'extensions/popup.js') -Raw
$serviceWorkerSource = Get-Content -LiteralPath (Join-Path $repo 'extensions/service-worker.js') -Raw
$offscreenSource = Get-Content -LiteralPath (Join-Path $repo 'extensions/offscreen.js') -Raw
$workletSource = Get-Content -LiteralPath (Join-Path $repo 'extensions/audio-worklet.js') -Raw
Assert-ExtensionSourcePolicy $popupSource $serviceWorkerSource $offscreenSource $workletSource 'extensions source'

Write-Output 'Browser extension source checks passed (MV3 permissions, exact CSP and user-gesture/loopback source boundaries verified).'
