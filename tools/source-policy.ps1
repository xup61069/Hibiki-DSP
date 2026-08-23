#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

$script:BlockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj', '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
$script:SecretPatterns = @('BEGIN PRIVATE KEY', 'GUMROAD_ACCESS_TOKEN', 'PARTNER_CENTER_TOKEN', 'ghp_', 'github_pat_')

function Get-SourcePolicyBlockedPaths {
  param([string[]]$Paths)
  return @($Paths | Where-Object { $script:BlockedExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() })
}

function Get-SourcePolicySecretViolation {
  param([string]$Text)
  foreach ($pattern in $script:SecretPatterns) {
    if ($Text -and $Text.Contains($pattern)) { return $pattern }
  }
  return $null
}

function Test-SourcePolicyPeHeader {
  param([byte[]]$Bytes)
  if ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0x4d -and $Bytes[1] -eq 0x5a) { return $true }
  return $false
}

if ($SelfTest) {
  $caseCount = 0

  # Case 1: allowed text paths produce no blocked results.
  $allowed = @(Get-SourcePolicyBlockedPaths -Paths @('README.md', 'tools/tool.ps1', 'schemas/schema.json', 'docs/guide.yml', 'installer/HibikiSetup.ps1'))
  if ($allowed.Count -ne 0) { throw "SelfTest case 'allowed-text-paths' flagged: $($allowed -join ', ')" }
  $caseCount++

  # Case 2: single blocked extension .exe is flagged.
  $blocked = @(Get-SourcePolicyBlockedPaths -Paths @('tools/a.exe'))
  if ($blocked.Count -ne 1 -or $blocked[0] -ne 'tools/a.exe') { throw "SelfTest case 'single-exe' failed: $($blocked -join ', ')" }
  $caseCount++

  # Case 3: multiple blocked extensions flagged preserving input order.
  $blocked = @(Get-SourcePolicyBlockedPaths -Paths @('z.exe', 'keep.md', 'a.dll', 'b.sys', 'c.txt'))
  if ($blocked.Count -ne 3 -or $blocked[0] -ne 'z.exe' -or $blocked[1] -ne 'a.dll' -or $blocked[2] -ne 'b.sys') { throw "SelfTest case 'multiple-blocked-order' failed: $($blocked -join ', ')" }
  $caseCount++

  # Case 4: case-insensitive extension match (.EXE, .Dll).
  $blocked = @(Get-SourcePolicyBlockedPaths -Paths @('UPPER/X.EXE', 'mixed/Y.Dll', 'lower/z.so'))
  if ($blocked.Count -ne 3) { throw "SelfTest case 'case-insensitive' flagged $($blocked.Count) expected 3: $($blocked -join ', ')" }
  $caseCount++

  # Case 5: every blocked extension class is flagged.
  $samples = @('a.exe', 'b.dll', 'c.sys', 'd.msi', 'e.msix', 'f.vst3', 'g.cat', 'h.pdb', 'i.obj', 'j.lib', 'k.pfx', 'l.key', 'm.pem', 'n.cab', 'o.zip', 'p.7z', 'q.bin', 'r.so', 's.dylib')
  $blocked = @(Get-SourcePolicyBlockedPaths -Paths $samples)
  if ($blocked.Count -ne $samples.Count) { throw "SelfTest case 'all-extensions' flagged $($blocked.Count) expected $($samples.Count)" }
  $caseCount++

  # Case 6: secret pattern BEGIN PRIVATE KEY is detected.
  $violation = Get-SourcePolicySecretViolation -Text '-----BEGIN PRIVATE KEY----- dummy'
  if ($violation -ne 'BEGIN PRIVATE KEY') { throw "SelfTest case 'secret-private-key' failed: $violation" }
  $caseCount++

  # Case 7: secret pattern ghp_ is detected.
  $violation = Get-SourcePolicySecretViolation -Text 'token ghp_abc123'
  if ($violation -ne 'ghp_') { throw "SelfTest case 'secret-ghp' failed: $violation" }
  $caseCount++

  # Case 8: secret pattern GUMROAD_ACCESS_TOKEN is detected.
  $violation = Get-SourcePolicySecretViolation -Text 'GUMROAD_ACCESS_TOKEN=secret'
  if ($violation -ne 'GUMROAD_ACCESS_TOKEN') { throw "SelfTest case 'secret-gumroad' failed: $violation" }
  $caseCount++

  # Case 9: clean text produces no secret violation.
  $violation = Get-SourcePolicySecretViolation -Text 'This is a normal README without secrets.'
  if ($null -ne $violation) { throw "SelfTest case 'clean-text' flagged: $violation" }
  $caseCount++

  # Case 10: PE header detection (MZ).
  if (-not (Test-SourcePolicyPeHeader -Bytes ([byte[]]@(0x4d, 0x5a, 0x90, 0x00)))) { throw "SelfTest case 'pe-header-positive' failed" }
  if (Test-SourcePolicyPeHeader -Bytes ([byte[]]@(0x00, 0x00))) { throw "SelfTest case 'pe-header-negative' false positive" }
  $caseCount++

  Write-Output "Source-only policy gate self-test passed ($caseCount cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$tracked = @()
$usingFallbackScan = $false
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) {
  $inside = git -C $repo rev-parse --is-inside-work-tree 2>$null
  if ($inside -eq 'true') {
    $tracked = @(git -C $repo ls-files)
  }
}
if ($tracked.Count -eq 0) {
  $usingFallbackScan = $true
  $tracked = @(Get-ChildItem -LiteralPath $repo -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($repo.Length + 1)
    $segments = $relative -split '\\'
    if ($segments -notcontains '.local' -and $segments -notcontains '.git' -and
        $segments -notcontains 'bin' -and $segments -notcontains 'obj') { $relative }
  })
}

$blocked = @(Get-SourcePolicyBlockedPaths -Paths $tracked)
if ($blocked.Count -gt 0) { throw "Source-only policy violation: $($blocked -join ', ')" }

foreach ($path in $tracked) {
  if ((Split-Path -Leaf $path) -eq 'source-policy.ps1') { continue }
  $full = Join-Path $repo $path
  if (-not (Test-Path -LiteralPath $full)) { continue }
  $bytes = [IO.File]::ReadAllBytes($full)
  if (Test-SourcePolicyPeHeader -Bytes $bytes) {
    throw "PE executable header found in $path"
  }
  $text = Get-Content -LiteralPath $full -Raw -ErrorAction SilentlyContinue
  $violation = Get-SourcePolicySecretViolation -Text $text
  if ($violation) { throw "Possible secret pattern '$violation' found in $path" }
}

$scope = if ($usingFallbackScan) { 'scanned' } else { 'tracked' }
Write-Output "Source-only policy passed for $($tracked.Count) $scope paths."
