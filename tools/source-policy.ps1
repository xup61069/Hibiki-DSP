[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$blockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj', '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
$secretPatterns = @('BEGIN PRIVATE KEY', 'GUMROAD_ACCESS_TOKEN', 'PARTNER_CENTER_TOKEN', 'ghp_', 'github_pat_')

function Get-SourcePolicyViolation {
  param(
    [Parameter(Mandatory)] [string]$Path,
    [Parameter(Mandatory)] [byte[]]$Bytes,
    [AllowNull()] [string]$Text
  )

  $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
  if ($blockedExtensions -contains $extension) {
    return "Source-only policy violation: blocked extension in $Path"
  }

  if ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0x4d -and $Bytes[1] -eq 0x5a) {
    return "PE executable header found in $Path"
  }

  foreach ($pattern in $secretPatterns) {
    if ($Text -and $Text.Contains($pattern)) {
      return "Possible secret pattern '$pattern' found in $Path"
    }
  }

  return $null
}

function Assert-SourcePolicyPath {
  param(
    [Parameter(Mandatory)] [string]$Path,
    [Parameter(Mandatory)] [byte[]]$Bytes,
    [AllowNull()] [string]$Text
  )

  $violation = Get-SourcePolicyViolation -Path $Path -Bytes $Bytes -Text $Text
  if ($violation) { throw $violation }
}

function Invoke-SourcePolicySelfTest {
  $utf8 = [Text.Encoding]::UTF8
  $cases = @(
    [pscustomobject]@{ Name = 'valid-text'; Path = 'selftest-valid.txt'; Bytes = $utf8.GetBytes('plain source text'); Text = 'plain source text'; Reject = $false; Expected = '' },
    [pscustomobject]@{ Name = 'allowed-text-extension'; Path = 'selftest-allowed.MD'; Bytes = $utf8.GetBytes('# source'); Text = '# source'; Reject = $false; Expected = '' },
    [pscustomobject]@{ Name = 'blocked-extension-uppercase'; Path = 'selftest-blocked.EXE'; Bytes = [byte[]](0x00, 0x01); Text = ''; Reject = $true; Expected = 'blocked extension' },
    [pscustomobject]@{ Name = 'blocked-extension-key'; Path = 'selftest-private.PEM'; Bytes = [byte[]](0x00, 0x01); Text = ''; Reject = $true; Expected = 'blocked extension' },
    [pscustomobject]@{ Name = 'pe-header'; Path = 'selftest-pe.txt'; Bytes = [byte[]](0x4d, 0x5a, 0x90, 0x00); Text = ''; Reject = $true; Expected = 'PE executable header' }
  )

  foreach ($pattern in $secretPatterns) {
    $cases += [pscustomobject]@{
      Name = "secret-$pattern"
      Path = 'selftest-secret.txt'
      Bytes = $utf8.GetBytes("prefix $pattern suffix")
      Text = "prefix $pattern suffix"
      Reject = $true
      Expected = "Possible secret pattern '$pattern'"
    }
  }

  $passed = 0
  foreach ($case in $cases) {
    $caught = $false
    $errorMessage = ''
    try {
      Assert-SourcePolicyPath -Path $case.Path -Bytes $case.Bytes -Text $case.Text
    } catch {
      $caught = $true
      $errorMessage = $_.Exception.Message
    }

    if ($case.Reject -and -not $caught) {
      throw "Source-only policy self-test expected rejection: $($case.Name)"
    }
    if (-not $case.Reject -and $caught) {
      throw "Source-only policy self-test unexpectedly rejected $($case.Name): $errorMessage"
    }
    if ($case.Expected -and $errorMessage -notlike "*$($case.Expected)*") {
      throw "Source-only policy self-test reported the wrong rejection for $($case.Name): $errorMessage"
    }
    $passed++
  }

  Write-Output "Source-only policy self-test passed ($passed cases)."
}

if ($SelfTest) {
  Invoke-SourcePolicySelfTest
  return
}

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

$blocked = @($tracked | Where-Object { $blockedExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() })
if ($blocked.Count -gt 0) { throw "Source-only policy violation: $($blocked -join ', ')" }

foreach ($path in $tracked) {
  if ((Split-Path -Leaf $path) -eq 'source-policy.ps1') { continue }
  $full = Join-Path $repo $path
  if (-not (Test-Path -LiteralPath $full)) { continue }
  $bytes = [IO.File]::ReadAllBytes($full)
  $text = Get-Content -LiteralPath $full -Raw -ErrorAction SilentlyContinue
  Assert-SourcePolicyPath -Path $path -Bytes $bytes -Text $text
}

$scope = if ($usingFallbackScan) { 'scanned' } else { 'tracked' }
Write-Output "Source-only policy passed for $($tracked.Count) $scope paths."
