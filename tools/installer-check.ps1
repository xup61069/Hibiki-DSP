[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$script:RequiredBoundaries = @(
  'Read-ReleaseManifest',
  'Test-ManifestFiles',
  'Invoke-HibikiInstall',
  '-Apply',
  'pnputil.exe',
  'IsPathRooted',
  'sha256',
  'dependency_lock_digest',
  'driver_package',
  'microsoft_signature_thumbprint',
  'rfc3161_timestamp',
  'sbom_digest'
)

function Assert-InstallerSourcePolicy {
  param(
    [string]$Text,
    [string]$SourceName
  )
  foreach ($required in $script:RequiredBoundaries) {
    if (-not $Text.Contains($required)) {
      throw "Installer source missing required boundary in ${SourceName}: $required"
    }
  }
}

function Assert-InstallerParseInput {
  param(
    [string]$Text,
    [string]$SourceName
  )
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseInput($Text, [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) {
    throw "Installer PowerShell parse errors in ${SourceName}: $($errors -join '; ')"
  }
}

function Assert-InstallerParseFile {
  param(
    [string]$Path
  )
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) {
    throw "Installer PowerShell parse errors: $($errors -join '; ')"
  }
}

if ($SelfTest) {
  $validFixture = @'
function Read-ReleaseManifest {}
function Test-ManifestFiles {}
function Invoke-HibikiInstall { param([switch]$Apply) }
$null = "-Apply"
$null = "pnputil.exe"
$null = [IO.Path]::IsPathRooted("test")
$null = "sha256"
$null = "dependency_lock_digest"
$null = "driver_package"
$null = "microsoft_signature_thumbprint"
$null = "rfc3161_timestamp"
$null = "sbom_digest"
'@

  $caseCount = 0

  # Case 1: valid fixture parses and satisfies all boundaries.
  Assert-InstallerParseInput -Text $validFixture -SourceName 'selftest-valid'
  Assert-InstallerSourcePolicy -Text $validFixture -SourceName 'selftest-valid'
  $caseCount++

  # Case 2: missing Read-ReleaseManifest is rejected.
  $missingOne = $validFixture.Replace('Read-ReleaseManifest', 'MissingBoundary')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingOne -SourceName 'selftest-missing-Read-ReleaseManifest' } catch { $caught = $true; if ("$($_.Exception.Message)" -notmatch 'Read-ReleaseManifest') { throw "SelfTest missing-boundary case failed with unexpected message: $($_.Exception.Message)" } }
  if (-not $caught) { throw 'SelfTest expected missing Read-ReleaseManifest failure.' }
  $caseCount++

  # Case 3: missing sha256 is rejected.
  $missingSha = $validFixture.Replace('sha256', 'missingHash')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingSha -SourceName 'selftest-missing-sha256' } catch { $caught = $true; if ("$($_.Exception.Message)" -notmatch 'sha256') { throw } }
  if (-not $caught) { throw 'SelfTest expected missing sha256 failure.' }
  $caseCount++

  # Case 4: missing pnputil.exe is rejected.
  $missingPnputil = $validFixture.Replace('pnputil.exe', 'missing.exe')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingPnputil -SourceName 'selftest-missing-pnputil' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing pnputil.exe failure.' }
  $caseCount++

  # Case 5: missing IsPathRooted is rejected (path-traversal guard).
  $missingRooted = $validFixture.Replace('IsPathRooted', 'MissingGuard')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingRooted -SourceName 'selftest-missing-IsPathRooted' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing IsPathRooted failure.' }
  $caseCount++

  # Case 6: missing microsoft_signature_thumbprint is rejected.
  $missingSig = $validFixture.Replace('microsoft_signature_thumbprint', 'missingThumbprint')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingSig -SourceName 'selftest-missing-thumbprint' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing microsoft_signature_thumbprint failure.' }
  $caseCount++

  # Case 7: PowerShell parse error is rejected.
  $invalidSyntax = 'function { invalid syntax !@# }'
  $caught = $false
  try { Assert-InstallerParseInput -Text $invalidSyntax -SourceName 'selftest-parse-error' } catch { $caught = $true; if ("$($_.Exception.Message)" -notmatch 'parse errors') { throw } }
  if (-not $caught) { throw 'SelfTest expected parse-error failure.' }
  $caseCount++

  # Case 8: missing rfc3161_timestamp is rejected.
  $missingRfc = $validFixture.Replace('rfc3161_timestamp', 'missingTimestamp')
  $caught = $false
  try { Assert-InstallerSourcePolicy -Text $missingRfc -SourceName 'selftest-missing-rfc3161' } catch { $caught = $true }
  if (-not $caught) { throw 'SelfTest expected missing rfc3161_timestamp failure.' }
  $caseCount++

  Write-Output "Installer source gate self-test passed ($caseCount cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'installer/HibikiSetup.ps1'
Assert-InstallerParseFile -Path $path
$text = Get-Content -LiteralPath $path -Raw
Assert-InstallerSourcePolicy -Text $text -SourceName 'installer/HibikiSetup.ps1'
Write-Output 'Installer source checks passed.'
