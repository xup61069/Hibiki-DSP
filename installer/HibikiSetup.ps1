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
  if ([string]::IsNullOrWhiteSpace($manifest.toolchain_digest) -or
      $manifest.toolchain_digest -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Manifest toolchain_digest must be a SHA-256 digest.'
  }
  if ([string]::IsNullOrWhiteSpace($manifest.dependency_lock_digest) -or
      $manifest.dependency_lock_digest -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Manifest dependency_lock_digest must be a SHA-256 digest.'
  }
  if ([string]::IsNullOrWhiteSpace($manifest.sbom_digest) -or
      $manifest.sbom_digest -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Manifest sbom_digest must be a SHA-256 digest.'
  }
  if ($null -eq $manifest.driver_package -or
      $manifest.driver_package.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
      $manifest.driver_package.catalog_sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
      $manifest.driver_package.microsoft_signature_thumbprint -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Manifest driver_package must carry package/catalog hashes and Microsoft signature thumbprint.'
  }
  if ($null -eq $manifest.installer -or
      $manifest.installer.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
      $manifest.installer.signer_thumbprint -notmatch '^[0-9a-fA-F]{40}$' -or
      [string]::IsNullOrWhiteSpace($manifest.installer.rfc3161_timestamp)) {
    throw 'Manifest installer must carry hash, signer thumbprint and RFC3161 timestamp.'
  }
  return $manifest
}

function Test-ManifestFiles($Manifest, [string]$Root) {
  foreach ($entry in @($Manifest.unsigned_files)) {
    if ([string]::IsNullOrWhiteSpace($entry.path) -or
        [IO.Path]::IsPathRooted($entry.path) -or
        $entry.path -match '(^|[\\/])\.\.([\\/]|$)' -or
        $entry.path -match '^[\\/]') {
      throw "Manifest path must stay relative to PackageRoot: $($entry.path)"
    }
    if ([string]::IsNullOrWhiteSpace($entry.sha256) -or
        $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
      throw "Manifest sha256 must be a 64-character hexadecimal digest: $($entry.path)"
    }
    $path = Join-Path $Root $entry.path
    $resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path).TrimEnd('\') + '\'
    $resolvedPath = [IO.Path]::GetFullPath($path)
    if (-not $resolvedPath.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Manifest path escapes PackageRoot: $($entry.path)"
    }
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
