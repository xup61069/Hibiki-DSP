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
  if ($csp -match 'unsafe-eval') {
    throw "CSP contains unsafe-eval in $sourceName."
  }
  if ($csp -match 'unsafe-inline') {
    throw "CSP contains unsafe-inline in $sourceName."
  }
  if ($csp -notmatch "script-src\s+'self'") {
    throw "CSP script-src must be strictly 'self' in $sourceName."
  }
  if ($csp -notmatch "object-src\s+('self'|'none')") {
    throw "CSP object-src must be 'self' or 'none' in $sourceName."
  }
  if ($csp -notmatch "connect-src\s+ws:\/\/127\.0\.0\.1:17842(?:\s*;|\s*$)") {
    throw "CSP connect-src must be strictly ws://127.0.0.1:17842 without wildcard origins in $sourceName."
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

  $multilineCsp = $validJson | ConvertFrom-Json
  $multilineCsp.content_security_policy.extension_pages = @"
script-src    'self' ;
object-src    'none' ;
connect-src   ws://127.0.0.1:17842
"@
  Assert-ExtensionManifestPolicy $multilineCsp 'selftest-multiline-csp'

  Write-Output 'Browser extension policy self-test passed (10 cases).'
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

Write-Output 'Browser extension source checks passed (MV3 least-privilege permissions, host allowlist, and self-only CSP verified).'
