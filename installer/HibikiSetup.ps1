[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)][string]$PackageRoot,
  [Parameter(Mandatory = $true)][string]$ManifestPath,
  [switch]$Apply
)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-ReleaseManifest([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { throw "Manifest not found: $Path" }
  $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  if ($manifest.schema_version -ne 1) { throw 'Unsupported ReleaseManifest schema.' }
  if ($manifest.source_tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+') {
    throw 'Manifest source_tag is not a stable version tag.'
  }
  if ([string]::IsNullOrWhiteSpace($manifest.source_commit) -or
      $manifest.source_commit -notmatch '^[0-9a-f]{40}$') {
    throw 'Manifest source_commit must be a 40-character commit.'
  }
  return $manifest
}

function Test-ManifestFiles($Manifest, [string]$Root) {
  foreach ($entry in @($Manifest.unsigned_files)) {
    $path = Join-Path $Root $entry.path
    if (-not (Test-Path -LiteralPath $path)) { throw "Manifest file missing: $($entry.path)" }
    $actual = Get-Sha256 $path
    if ($actual -ne $entry.sha256.ToLowerInvariant()) {
      throw "Hash mismatch for $($entry.path)."
    }
  }
  return $true
}

function Invoke-HibikiInstall {
  [CmdletBinding(SupportsShouldProcess)]
  param(
    [string]$Root,
    $Manifest
  )
  $driverInfs = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter '*.inf' -File)
  if ($driverInfs.Count -eq 0) { throw 'No driver INF found in the supplied package.' }
  $driverInfs | ForEach-Object {
    if ($PSCmdlet.ShouldProcess($_.FullName, 'Stage signed Hibiki driver')) {
      & pnputil.exe /add-driver $_.FullName /install
      if ($LASTEXITCODE -ne 0) { throw "PnPUtil failed for $($_.Name): $LASTEXITCODE" }
    }
  }
  Write-Output "Hibiki $($Manifest.product_version) driver transaction completed."
}

$manifest = Read-ReleaseManifest $ManifestPath
Test-ManifestFiles $manifest $PackageRoot | Out-Null
Write-Output "Verified source tag $($manifest.source_tag), commit $($manifest.source_commit)."

if (-not $Apply) {
  Write-Output 'Dry-run only. Re-run with -Apply after reviewing the signed package.'
  return
}

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Administrator privileges are required for -Apply.'
}
Invoke-HibikiInstall $PackageRoot $manifest
