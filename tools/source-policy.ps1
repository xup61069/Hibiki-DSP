[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
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
