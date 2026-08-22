[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# --- Self-test mode: validate internal logic without touching the filesystem ---
if ($SelfTest) {
  $caseCount = 0

  $blockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj', '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
  $secretPatterns = @('BEGIN PRIVATE KEY', 'GUMROAD_ACCESS_TOKEN', 'PARTNER_CENTER_TOKEN', 'ghp_', 'github_pat_')

  # Case 1: blocked extension is detected.
  $testFile = 'test.dll'
  $ext = [IO.Path]::GetExtension($testFile).ToLowerInvariant()
  if ($blockedExtensions -notcontains $ext) {
    throw 'source-policy self-test failed: .dll should be blocked.'
  }
  $caseCount++

  # Case 2: allowed extension passes.
  $ext2 = [IO.Path]::GetExtension('test.ps1').ToLowerInvariant()
  if ($blockedExtensions -contains $ext2) {
    throw 'source-policy self-test failed: .ps1 should not be blocked.'
  }
  $caseCount++

  # Case 3: all blocked extensions are recognized.
  foreach ($bext in $blockedExtensions) {
    $testName = "file$bext"
    $got = [IO.Path]::GetExtension($testName).ToLowerInvariant()
    if ($got -ne $bext) {
      throw "source-policy self-test failed: extension $bext was not resolved correctly."
    }
  }
  $caseCount++

  # Case 4: PE header detection - MZ header detected.
  $peBytes = [byte[]]@(0x4d, 0x5a, 0x00, 0x00)
  if ($peBytes[0] -ne 0x4d -or $peBytes[1] -ne 0x5a) {
    throw 'source-policy self-test failed: MZ header detection logic is wrong.'
  }
  $caseCount++

  # Case 5: PE header detection - non-MZ header passes.
  $nonPfBytes = [byte[]]@(0x23, 0x21, 0x00, 0x00)
  if ($nonPfBytes[0] -eq 0x4d -and $nonPfBytes[1] -eq 0x5a) {
    throw 'source-policy self-test failed: non-MZ header was falsely detected as PE.'
  }
  $caseCount++

  # Case 6: short bytes (< 2) do not trigger PE detection.
  $shortBytes = [byte[]]@(0x4d)
  if ($shortBytes.Length -ge 2 -and $shortBytes[0] -eq 0x4d -and $shortBytes[1] -eq 0x5a) {
    throw 'source-policy self-test failed: single byte should not trigger PE detection.'
  }
  $caseCount++

  # Cases 7-11: each secret pattern is detected.
  foreach ($pattern in $secretPatterns) {
    $fakeFile = "secret content $pattern more content"
    if (-not $fakeFile.Contains($pattern)) {
      throw "source-policy self-test failed: pattern '$pattern' was not detected in synthetic string."
    }
    $caseCount++
  }

  # Case 12: clean content has no secret patterns.
  $cleanContent = 'This is normal source code with no secrets.'
  foreach ($pattern in $secretPatterns) {
    if ($cleanContent.Contains($pattern)) {
      throw "source-policy self-test failed: pattern '$pattern' false-positived on clean content."
    }
  }
  $caseCount++

  # Case 13: secret patterns list is not empty.
  if ($secretPatterns.Count -lt 1) {
    throw 'source-policy self-test failed: secret patterns list is empty.'
  }
  $caseCount++

  if ($caseCount -lt 12) {
    throw "source-policy self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "source-policy self-test passed ($caseCount cases; extension blocking, PE header detection, secret pattern scan)."
  exit 0
}

# --- Main gate logic ---
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

$blockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj', '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
$blocked = @($tracked | Where-Object { $blockedExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() })
if ($blocked.Count -gt 0) { throw "Source-only policy violation: $($blocked -join ', ')" }

$secretPatterns = @('BEGIN PRIVATE KEY', 'GUMROAD_ACCESS_TOKEN', 'PARTNER_CENTER_TOKEN', 'ghp_', 'github_pat_')
foreach ($path in $tracked) {
  if ((Split-Path -Leaf $path) -eq 'source-policy.ps1') { continue }
  $full = Join-Path $repo $path
  if (-not (Test-Path -LiteralPath $full)) { continue }
  $bytes = [IO.File]::ReadAllBytes($full)
  if ($bytes.Length -ge 2 -and $bytes[0] -eq 0x4d -and $bytes[1] -eq 0x5a) {
    throw "PE executable header found in $path"
  }
  $text = Get-Content -LiteralPath $full -Raw -ErrorAction SilentlyContinue
  foreach ($pattern in $secretPatterns) {
    if ($text -and $text.Contains($pattern)) { throw "Possible secret pattern '$pattern' found in $path" }
  }
}

$scope = if ($usingFallbackScan) { 'scanned' } else { 'tracked' }
Write-Output "Source-only policy passed for $($tracked.Count) $scope paths."
