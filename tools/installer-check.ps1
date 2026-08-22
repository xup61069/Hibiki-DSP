[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# --- Self-test mode: validate internal logic without touching the filesystem ---
if ($SelfTest) {
  $caseCount = 0

  $requiredTokens = @('Read-ReleaseManifest', 'Test-ManifestFiles', 'Invoke-HibikiInstall', '-Apply', 'pnputil.exe', 'IsPathRooted', 'sha256', 'dependency_lock_digest', 'driver_package', 'microsoft_signature_thumbprint', 'rfc3161_timestamp', 'sbom_digest')

  # Helper: test that a string passes the token check.
  function Test-TokenCheck([string]$text, [string[]]$required) {
    foreach ($tok in $required) {
      if (-not $text.Contains($tok)) { return $false }
    }
    return $true
  }

  # Case 1: string containing all tokens passes.
  $allTokens = $requiredTokens -join ' '
  if (-not (Test-TokenCheck -text $allTokens -required $requiredTokens)) {
    throw 'installer-check self-test failed: all-tokens string should pass.'
  }
  $caseCount++

  # Cases 2-13: each missing token is detected.
  foreach ($missing in $requiredTokens) {
    $subset = ($requiredTokens | Where-Object { $_ -ne $missing }) -join ' '
    if (Test-TokenCheck -text $subset -required $requiredTokens) {
      throw "installer-check self-test failed: missing '$missing' was not detected."
    }
    $caseCount++
  }

  # Case 14: empty string fails.
  if (Test-TokenCheck -text '' -required $requiredTokens) {
    throw 'installer-check self-test failed: empty string should fail.'
  }
  $caseCount++

  # Case 15: partial match (e.g. 'Apply' without dash prefix) still contains '-Apply' substring check.
  if (-not (Test-TokenCheck -text 'token -Apply end' -required @('-Apply'))) {
    throw 'installer-check self-test failed: substring check for -Apply should work.'
  }
  $caseCount++

  if ($caseCount -lt 12) {
    throw "installer-check self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "installer-check self-test passed ($caseCount cases; token presence/absence validation)."
  exit 0
}

# --- Main gate logic ---
$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'installer/HibikiSetup.ps1'
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { throw "Installer PowerShell parse errors: $($errors -join '; ')" }
$text = Get-Content -LiteralPath $path -Raw
foreach ($required in @('Read-ReleaseManifest', 'Test-ManifestFiles', 'Invoke-HibikiInstall', '-Apply', 'pnputil.exe', 'IsPathRooted', 'sha256', 'dependency_lock_digest', 'driver_package', 'microsoft_signature_thumbprint', 'rfc3161_timestamp', 'sbom_digest')) {
  if (-not $text.Contains($required)) { throw "Installer source missing required boundary: $required" }
}
Write-Output 'Installer source checks passed.'
