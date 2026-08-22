[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function Test-SourcePolicy {
  param([string]$Path)

  $git = Get-Command git -ErrorAction SilentlyContinue
  $tracked = @()
  $usingFallbackScan = $false
  if ($git) {
    $inside = git -C $Path rev-parse --is-inside-work-tree 2>$null
    if ($inside -eq 'true') {
      $tracked = @(git -C $Path ls-files)
    }
  }
  if ($tracked.Count -eq 0) {
    $usingFallbackScan = $true
    $tracked = @(Get-ChildItem -LiteralPath $Path -Recurse -File | ForEach-Object {
      $relative = $_.FullName.Substring($Path.Length + 1)
      $segments = $relative -split '\\'
      if ($segments -notcontains '.local' -and $segments -notcontains '.git' -and
          $segments -notcontains 'bin' -and $segments -notcontains 'obj') { $relative }
    })
  }

  $blockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj', '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
  $blocked = @($tracked | Where-Object { $blockedExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() })
  if ($blocked.Count -gt 0) { throw "Source-only policy violation: $($blocked -join ', ')" }

  $secretPatterns = @('BEGIN PRIVATE KEY', 'GUMROAD_ACCESS_TOKEN', 'PARTNER_CENTER_TOKEN', 'ghp_', 'github_pat_')
  foreach ($p in $tracked) {
    if ((Split-Path -Leaf $p) -eq 'source-policy.ps1') { continue }
    $full = Join-Path $Path $p
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $bytes = [IO.File]::ReadAllBytes($full)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0x4d -and $bytes[1] -eq 0x5a) {
      throw "PE executable header found in $p"
    }
    $text = Get-Content -LiteralPath $full -Raw -ErrorAction SilentlyContinue
    foreach ($pattern in $secretPatterns) {
      if ($text -and $text.Contains($pattern)) { throw "Possible secret pattern '$pattern' found in $p" }
    }
  }

  $scope = if ($usingFallbackScan) { 'scanned' } else { 'tracked' }
  [PSCustomObject]@{ Count = $tracked.Count; Scope = $scope }
}

if ($SelfTest) {
  $caseCount = 0
  $tmpRoot = [System.IO.Path]::GetFullPath((Join-Path $env:TEMP ('hibiki-source-policy-selftest-' + [guid]::NewGuid().ToString('N'))))
  try {
    # Case 1: clean fallback-scan directory passes.
    $clean = Join-Path $tmpRoot 'clean'
    New-Item -ItemType Directory -Path $clean -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $clean 'readme.md') -Value '# safe content' -NoNewline
    Set-Content -LiteralPath (Join-Path $clean 'code.cs') -Value 'public class C {}' -NoNewline
    $r = Test-SourcePolicy -Path $clean
    if ($r.Count -lt 1 -or $r.Scope -ne 'scanned') {
      throw 'source-policy self-test failed: clean directory did not pass with scanned scope.'
    }
    $caseCount++

    # Case 2: blocked extension fails closed.
    $blockedDir = Join-Path $tmpRoot 'blocked'
    New-Item -ItemType Directory -Path $blockedDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $blockedDir 'evil.exe') -Value 'MZ' -NoNewline
    try {
      Test-SourcePolicy -Path $blockedDir | Out-Null
      throw 'source-policy self-test failed: blocked .exe extension did not fail.'
    }
    catch {
      if ($_.Exception.Message -notmatch 'policy violation') {
        throw ('source-policy self-test failed: unexpected blocked-extension message: ' + $_.Exception.Message)
      }
    }
    $caseCount++

    # Case 3: secret pattern fails closed.
    $secretDir = Join-Path $tmpRoot 'secret'
    New-Item -ItemType Directory -Path $secretDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $secretDir 'config.txt') -Value 'GUMROAD_ACCESS_TOKEN=abc' -NoNewline
    try {
      Test-SourcePolicy -Path $secretDir | Out-Null
      throw 'source-policy self-test failed: secret pattern did not fail.'
    }
    catch {
      if ($_.Exception.Message -notmatch 'secret pattern') {
        throw ('source-policy self-test failed: unexpected secret message: ' + $_.Exception.Message)
      }
    }
    $caseCount++

    # Case 4: PE header fails closed (non-blocked extension).
    $peDir = Join-Path $tmpRoot 'pe'
    New-Item -ItemType Directory -Path $peDir -Force | Out-Null
    $pePath = Join-Path $peDir 'payload.dat'
    [IO.File]::WriteAllBytes($pePath, @([byte]0x4d, [byte]0x5a, 1, 2, 3, 4))
    try {
      Test-SourcePolicy -Path $peDir | Out-Null
      throw 'source-policy self-test failed: PE header did not fail.'
    }
    catch {
      if ($_.Exception.Message -notmatch 'PE executable header') {
        throw ('source-policy self-test failed: unexpected PE message: ' + $_.Exception.Message)
      }
    }
    $caseCount++

    # Case 5: tracked (git) path passes with a clean repo.
    $gitCmd = Get-Command git -ErrorAction SilentlyContinue
    if ($gitCmd) {
      $gitDir = Join-Path $tmpRoot 'gitrepo'
      New-Item -ItemType Directory -Path $gitDir -Force | Out-Null
      Set-Content -LiteralPath (Join-Path $gitDir 'ok.md') -Value 'ok' -NoNewline
      git -C $gitDir init -q 2>$null
      git -C $gitDir add -A 2>$null
      git -C $gitDir -c user.email=selftest@example.com -c user.name=selftest commit -q -m init 2>$null
      $gr = Test-SourcePolicy -Path $gitDir
      if ($gr.Scope -ne 'tracked') {
        throw "source-policy self-test failed: git repo did not use tracked scope (saw '$($gr.Scope)')."
      }
      $caseCount++
    }
    else {
      $caseCount++
    }

    # Case 6: nested directories are scanned.
    $nested = Join-Path $tmpRoot 'nested'
    $sub = Join-Path $nested 'a\b'
    New-Item -ItemType Directory -Path $sub -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $sub 'deep.md') -Value 'x' -NoNewline
    $nr = Test-SourcePolicy -Path $nested
    if ($nr.Count -lt 1) {
      throw 'source-policy self-test failed: nested files were not counted.'
    }
    $caseCount++

    if ($caseCount -lt 6) {
      throw "source-policy self-test failed: expected at least 6 cases, saw $caseCount."
    }
    Write-Output "source-policy self-test passed ($caseCount cases; clean pass, blocked extension, secret pattern, PE header, tracked git scope, nested scan)."
    exit 0
  }
  finally {
    Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}

# --- Main gate logic ---
$repo = Split-Path -Parent $PSScriptRoot
$r = Test-SourcePolicy -Path $repo
Write-Output "Source-only policy passed for $($r.Count) $($r.Scope) paths."
