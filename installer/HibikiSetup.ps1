[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)][string]$PackageRoot,
  [Parameter(Mandatory = $true)][string]$ManifestPath,
  [string]$DestinationPath = "%ProgramFiles%\Hibiki DSP",
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

function Resolve-HibikiDestination([string]$Requested) {
  if ([string]::IsNullOrWhiteSpace($Requested)) {
    throw 'Destination path cannot be blank.'
  }
  $expanded = [Environment]::ExpandEnvironmentVariables($Requested)
  $rawSegments = $expanded.Split([char[]]@('\', '/')) | Where-Object { $_ }
  foreach ($seg in $rawSegments) {
    if ($seg -eq '.' -or $seg -eq '..') {
      throw "Destination must not contain '.' or '..' segments: $Requested"
    }
  }
  $fullPath = [IO.Path]::GetFullPath($expanded)
  if (-not [IO.Path]::IsPathRooted($fullPath)) {
    throw "Destination must be an absolute path: $Requested"
  }
  $driveRoot = [IO.Path]::GetPathRoot($fullPath).TrimEnd('\')
  if ($fullPath.TrimEnd('\') -eq $driveRoot) {
    throw "Destination cannot be a drive root: $fullPath"
  }
  $windowsDir = [Environment]::GetFolderPath('Windows')
  $windowsFull = [IO.Path]::GetFullPath($windowsDir).TrimEnd('\')
  if ($fullPath.StartsWith($windowsFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
      $fullPath.TrimEnd('\').Equals($windowsFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must not be inside the Windows directory: $fullPath"
  }
  return $fullPath
}

function Get-PreservedDataPaths {
  $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
  $dataDir = Join-Path $localAppData 'Hibiki DSP'
  return @(
    @{ Name = 'session-route-rules-v1.json'; Path = (Join-Path $dataDir 'session-route-rules-v1.json'); Exists = (Test-Path -LiteralPath (Join-Path $dataDir 'session-route-rules-v1.json')) },
    @{ Name = 'scene-cards-v1.json'; Path = (Join-Path $dataDir 'scene-cards-v1.json'); Exists = (Test-Path -LiteralPath (Join-Path $dataDir 'scene-cards-v1.json')) }
  )
}

function Get-StagingPlan($Manifest, [string]$Root, [string]$Destination) {
  $resolvedRoot = ([IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path)).TrimEnd('\') + '\'
  $resolvedDest = ([IO.Path]::GetFullPath($Destination)).TrimEnd('\') + '\'
  $plan = @()
  foreach ($entry in @($Manifest.unsigned_files)) {
    $sourcePath = Join-Path $Root $entry.path
    $destPath = Join-Path $Destination $entry.path
    $resolvedSource = [IO.Path]::GetFullPath($sourcePath)
    $resolvedDestFile = [IO.Path]::GetFullPath($destPath)
    if (-not $resolvedSource.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Staging source escapes PackageRoot: $($entry.path)"
    }
    if (-not $resolvedDestFile.StartsWith($resolvedDest, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Staging destination escapes DestinationPath: $($entry.path)"
    }
    $plan += @{
      RelativePath = $entry.path
      Source       = $resolvedSource
      Destination  = $resolvedDestFile
      Sha256       = $entry.sha256.ToLowerInvariant()
    }
  }
  return ,$plan
}

function Copy-HibikiFileWithHash {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Target,
    [Parameter(Mandatory)][string]$ExpectedSha256
  )
  $targetDir = Split-Path -Parent $Target
  if (-not (Test-Path -LiteralPath $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
  }
  $tempFile = Join-Path $targetDir ('.hibiki-tmp-' + [Guid]::NewGuid().ToString('N'))
  try {
    Copy-Item -LiteralPath $Source -Destination $tempFile
    $actualHash = (Get-FileHash -LiteralPath $tempFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $ExpectedSha256) {
      throw "Post-copy hash mismatch for $(Split-Path -Leaf $Target): expected $ExpectedSha256 got $actualHash"
    }
    Move-Item -LiteralPath $tempFile -Destination $Target -Force
  } finally {
    if ((Test-Path -LiteralPath $tempFile) -and ($tempFile -ne $Target)) {
      Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
    }
  }
}

function Invoke-PayloadStaging {
  [CmdletBinding()]
  param(
    [AllowEmptyCollection()][array]$Plan,
    [Parameter(Mandatory)][string]$Destination
  )
  $backupDirName = '.hibiki-backup-' + [Guid]::NewGuid().ToString('N')
  $backupDir = Join-Path $Destination $backupDirName
  $completedCopies = @()
  $backedUpFiles = @()
  $createdDirs = @()
  $backupCreated = $false
  try {
    if (-not (Test-Path -LiteralPath $Destination)) {
      New-Item -ItemType Directory -Path $Destination -Force | Out-Null
      $createdDirs += $Destination
    }
    foreach ($item in $Plan) {
      $destParent = Split-Path -Parent $item.Destination
      if (-not (Test-Path -LiteralPath $destParent)) {
        $dirsToCreate = @()
        $checkDir = $destParent
        while ($checkDir -and -not (Test-Path -LiteralPath $checkDir)) {
          $dirsToCreate += $checkDir
          $checkDir = Split-Path -Parent $checkDir
        }
        for ($i = $dirsToCreate.Count - 1; $i -ge 0; $i--) {
          New-Item -ItemType Directory -Path $dirsToCreate[$i] -Force | Out-Null
          $createdDirs += $dirsToCreate[$i]
        }
      }
      if (Test-Path -LiteralPath $item.Destination) {
        if (-not $backupCreated) {
          New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
          $backupCreated = $true
        }
        $backupSubPath = Join-Path $backupDir $item.RelativePath
        $backupSubDir = Split-Path -Parent $backupSubPath
        if (-not (Test-Path -LiteralPath $backupSubDir)) {
          New-Item -ItemType Directory -Path $backupSubDir -Force | Out-Null
        }
        Move-Item -LiteralPath $item.Destination -Destination $backupSubPath
        $backedUpFiles += @{ Original = $item.Destination; Backup = $backupSubPath }
      }
      Copy-HibikiFileWithHash -Source $item.Source -Target $item.Destination -ExpectedSha256 $item.Sha256
      $completedCopies += $item.Destination
    }
    if ($backupCreated -and (Test-Path -LiteralPath $backupDir)) {
      Remove-Item -LiteralPath $backupDir -Recurse -Force
    }
  } catch {
    for ($i = $completedCopies.Count - 1; $i -ge 0; $i--) {
      if (Test-Path -LiteralPath $completedCopies[$i]) {
        Remove-Item -LiteralPath $completedCopies[$i] -Force -ErrorAction SilentlyContinue
      }
    }
    for ($i = $backedUpFiles.Count - 1; $i -ge 0; $i--) {
      $bkEntry = $backedUpFiles[$i]
      if ((Test-Path -LiteralPath $bkEntry.Backup) -and -not (Test-Path -LiteralPath $bkEntry.Original)) {
        $origParent = Split-Path -Parent $bkEntry.Original
        if (-not (Test-Path -LiteralPath $origParent)) {
          New-Item -ItemType Directory -Path $origParent -Force | Out-Null
        }
        Move-Item -LiteralPath $bkEntry.Backup -Destination $bkEntry.Original -Force
      }
    }
    for ($i = $createdDirs.Count - 1; $i -ge 0; $i--) {
      if (Test-Path -LiteralPath $createdDirs[$i]) {
        $remaining = @(Get-ChildItem -LiteralPath $createdDirs[$i] -ErrorAction SilentlyContinue)
        if ($remaining.Count -eq 0) {
          Remove-Item -LiteralPath $createdDirs[$i] -Force -ErrorAction SilentlyContinue
        }
      }
    }
    throw
  }
}

function Invoke-HibikiInstall {
  [CmdletBinding(SupportsShouldProcess)]
  param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)]$Manifest,
    [Parameter(Mandatory)][string]$Destination
  )
  $driverInfs = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter '*.inf' -File)
  if ($driverInfs.Count -eq 0) { throw 'No driver INF found in the supplied package.' }
  $driverInfs | ForEach-Object {
    if ($PSCmdlet.ShouldProcess($_.FullName, 'Stage signed Hibiki driver')) {
      & pnputil.exe /add-driver $_.FullName /install
      if ($LASTEXITCODE -ne 0) { throw "PnPUtil failed for $($_.Name): $LASTEXITCODE" }
    }
  }
  $plan = Get-StagingPlan $Manifest $Root $Destination
  Invoke-PayloadStaging -Plan $plan -Destination $Destination
  Write-Output "Hibiki $($Manifest.product_version) driver and payload installation completed."
}

$manifest = Read-ReleaseManifest $ManifestPath
Test-ManifestFiles $manifest $PackageRoot | Out-Null
Write-Output "Verified source tag $($manifest.source_tag), commit $($manifest.source_commit)."

$destination = Resolve-HibikiDestination $DestinationPath
$stagingPlan = Get-StagingPlan $manifest $PackageRoot $destination
$preservedPaths = Get-PreservedDataPaths

Write-Output "Destination: $destination"
Write-Output "Planned payload files:"
foreach ($item in $stagingPlan) {
  Write-Output "  $($item.RelativePath)"
}
Write-Output "Preserved data paths:"
foreach ($p in $preservedPaths) {
  $status = if ($p.Exists) { '(exists)' } else { '(not found)' }
  Write-Output "  $($p.Name) $status"
}

if (-not $Apply) {
  Write-Output 'Dry-run only. No files or directories were created.'
  Write-Output 'Re-run with -Apply after reviewing the signed package.'
  return
}

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Administrator privileges are required for -Apply.'
}
Invoke-HibikiInstall $PackageRoot $manifest $destination
