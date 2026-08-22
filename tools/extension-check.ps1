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

  $multilineCsp = $validJson | ConvertFrom-Json
  $multilineCsp.content_security_policy.extension_pages = @"
script-src    'self' ;
object-src    'none' ;
connect-src   ws://127.0.0.1:17842
"@
  Assert-ExtensionManifestPolicy $multilineCsp 'selftest-multiline-csp'

  Write-Output 'Browser extension policy self-test passed (15 cases).'
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
