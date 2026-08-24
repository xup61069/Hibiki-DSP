[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)][string]$PackageRoot,
  [Parameter(Mandatory = $true)][string]$ManifestPath,
  [string]$DestinationPath = "%ProgramFiles%\Hibiki DSP",
  [switch]$Apply,
  [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Sha256([string]$Path) {
  (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-ReleaseManifest([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) { throw "Manifest not found: $Path" }
  $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  $printablePattern = '^[^\u0000-\u001F\u007F-\u009F]*$'

  # Reject unknown root-level fields (schema additionalProperties: false)
  $allowedRootFields = @(
    'schema_version', 'product_version', 'source_tag',
    'source_commit', 'distribution_id', 'toolchain_digest',
    'dependency_lock_digest', 'unsigned_files', 'driver_package',
    'installer', 'sbom_digest', 'tests', 'signed_installer_sha256'
  )
  foreach ($prop in ($manifest | Get-Member -MemberType NoteProperty).Name) {
    if ($allowedRootFields -notcontains $prop) {
      throw "Manifest contains unknown field: $prop"
    }
  }
  foreach ($required in @('unsigned_files', 'tests')) {
    if (-not ($manifest.PSObject.Properties.Name -contains $required)) {
      throw "Manifest is missing required field: $required"
    }
  }

  if ($manifest.schema_version -ne 1) { throw 'Unsupported ReleaseManifest schema.' }
  if ([string]::IsNullOrWhiteSpace($manifest.product_version)) {
    throw 'Manifest product_version must be a non-empty string.'
  }
  if (($manifest.product_version -is [string]) -and $manifest.product_version.Length -gt 64) {
    throw 'Manifest product_version exceeds maximum length of 64 characters.'
  }
  if (($manifest.product_version -is [string]) -and ($manifest.product_version -notmatch $printablePattern)) {
    throw 'Manifest product_version must not contain control characters.'
  }
  if ($manifest.source_tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+[A-Za-z0-9._:+-]{0,32}$') {
    throw 'Manifest source_tag must match v<major>.<minor>.<patch> with at most 32 allowed suffix characters.'
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
  foreach ($sectionName in @('driver_package', 'installer')) {
    $section = $manifest.$sectionName
    if ($null -eq $section) { continue }
    $allowedFields = @{
      driver_package = @('sha256', 'catalog_sha256', 'microsoft_signature_thumbprint')
      installer = @('sha256', 'signer_thumbprint', 'rfc3161_timestamp')
    }
    foreach ($prop in ($section | Get-Member -MemberType NoteProperty).Name) {
      if ($allowedFields[$sectionName] -notcontains $prop) {
        throw ("Manifest " + $sectionName + " contains unknown field: " + $prop)
      }
    }
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
  if (($manifest.installer.rfc3161_timestamp -is [string]) -and $manifest.installer.rfc3161_timestamp.Length -gt 128) {
    throw 'Manifest installer.rfc3161_timestamp exceeds maximum length of 128 characters.'
  }
  if (($manifest.installer.rfc3161_timestamp -is [string]) -and $manifest.installer.rfc3161_timestamp -notmatch $printablePattern) {
    throw 'Manifest installer.rfc3161_timestamp must not contain control characters.'
  }

  if (($manifest.PSObject.Properties.Name -contains 'distribution_id') -and
      [string]::IsNullOrWhiteSpace($manifest.distribution_id)) {
    throw 'Manifest distribution_id must be a non-empty string when present.'
  }
  if (($manifest.PSObject.Properties.Name -contains 'distribution_id') -and
      ($manifest.distribution_id -is [string]) -and $manifest.distribution_id -notmatch $printablePattern) {
    throw 'Manifest distribution_id must not contain control characters.'
  }

  if (($manifest.PSObject.Properties.Name -contains 'signed_installer_sha256')) {
    $sigHash = $manifest.signed_installer_sha256
    if ($null -eq $sigHash -or $sigHash -isnot [string] -or $sigHash -notmatch '^[0-9a-fA-F]{64}$') {
      throw 'Manifest signed_installer_sha256 must be a SHA-256 digest when present.'
    }
  }

  if ($null -eq $manifest.unsigned_files -or $manifest.unsigned_files -isnot [array]) {
    throw 'Manifest unsigned_files must be an array.'
  }
  if (@($manifest.unsigned_files).Count -gt 1024) {
    throw 'Manifest unsigned_files exceeds maximum count of 1024 entries.'
  }
  foreach ($entry in @($manifest.unsigned_files)) {
    if ($null -eq $entry -or $entry -isnot [pscustomobject]) {
      throw 'Manifest unsigned_files entries must be objects.'
    }
    foreach ($prop in ($entry | Get-Member -MemberType NoteProperty).Name) {
      if ($prop -notin @('path', 'sha256')) {
        throw ("Manifest unsigned_files entry contains unknown field: " + $prop)
      }
    }
    if (-not ($entry.PSObject.Properties.Name -contains 'path')) {
      throw 'Manifest unsigned_files entry is missing required path field.'
    }
    if (-not ($entry.PSObject.Properties.Name -contains 'sha256')) {
      throw 'Manifest unsigned_files entry is missing required sha256 field.'
    }
    if ($entry.path -isnot [string] -or $entry.path.Length -lt 1 -or $entry.path.Length -gt 260) {
      throw 'Manifest unsigned_files path must be a string of 1..260 characters.'
    }
    if ($entry.path -notmatch $printablePattern) {
      throw ('Manifest unsigned_files path must not contain control characters: ' + $entry.path)
    }
    if ($entry.sha256 -isnot [string] -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
      throw ("Manifest unsigned_files sha256 must be a 64-character hexadecimal digest: " + $entry.path)
    }
  }

  if ($null -eq $manifest.tests -or $manifest.tests -isnot [array]) {
    throw 'Manifest tests must be an array.'
  }
  if (@($manifest.tests).Count -gt 256) {
    throw 'Manifest tests exceeds maximum count of 256 entries.'
  }
  foreach ($test in @($manifest.tests)) {
    if ([string]::IsNullOrWhiteSpace($test) -or $test.Length -gt 120) {
      throw 'Manifest tests entries must be non-empty strings of at most 120 characters.'
    }
    if ($test -notmatch $printablePattern) {
      throw ('Manifest tests entry must not contain control characters.')
    }
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

function Get-UninstallPlan($Manifest, [string]$Destination) {
  $resolvedDest = ([IO.Path]::GetFullPath($Destination)).TrimEnd('\') + '\'
  $plan = @()
  foreach ($entry in @($Manifest.unsigned_files)) {
    $destPath = Join-Path $Destination $entry.path
    $resolvedDestFile = [IO.Path]::GetFullPath($destPath)
    if (-not $resolvedDestFile.StartsWith($resolvedDest, [StringComparison]::OrdinalIgnoreCase)) {
      throw "Uninstall destination escapes DestinationPath: $($entry.path)"
    }
    $plan += @{
      RelativePath = $entry.path
      Destination  = $resolvedDestFile
      Exists       = (Test-Path -LiteralPath $resolvedDestFile)
    }
  }
  return ,$plan
}

function Invoke-PayloadUninstall {
  [CmdletBinding()]
  param(
    [AllowEmptyCollection()][array]$Plan,
    [Parameter(Mandatory)][string]$Destination
  )
  $backupDirName = '.hibiki-uninstall-backup-' + [Guid]::NewGuid().ToString('N')
  $tempRoot = [IO.Path]::GetTempPath()
  $backupDir = Join-Path $tempRoot $backupDirName
  $removedFiles = @()
  $backedUpFiles = @()
  try {
    foreach ($item in $Plan) {
      if (-not $item.Exists) { continue }
      $backupSubPath = Join-Path $backupDir $item.RelativePath
      $backupSubDir = Split-Path -Parent $backupSubPath
      if (-not (Test-Path -LiteralPath $backupSubDir)) {
        New-Item -ItemType Directory -Path $backupSubDir -Force | Out-Null
      }
      Copy-Item -LiteralPath $item.Destination -Destination $backupSubPath -Force
      $backedUpFiles += @{ Original = $item.Destination; Backup = $backupSubPath }
      Remove-Item -LiteralPath $item.Destination -Force
      $removedFiles += $item.Destination
    }
    if ($removedFiles.Count -gt 0) {
      Remove-Item -LiteralPath $backupDir -Recurse -Force
    }
    Write-Output ("Removed $($removedFiles.Count) payload file(s).")
  } catch {
    for ($i = $removedFiles.Count - 1; $i -ge 0; $i--) {
      $bkEntry = $backedUpFiles | Where-Object { $_.Original -eq $removedFiles[$i] }
      if ($bkEntry -and (Test-Path -LiteralPath $bkEntry.Backup)) {
        $origParent = Split-Path -Parent $bkEntry.Original
        if (-not (Test-Path -LiteralPath $origParent)) {
          New-Item -ItemType Directory -Path $origParent -Force | Out-Null
        }
        Copy-Item -LiteralPath $bkEntry.Backup -Destination $bkEntry.Original -Force
      }
    }
    throw
  } finally {
    if (Test-Path -LiteralPath $backupDir) {
      Remove-Item -LiteralPath $backupDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
}

function Invoke-HibikiUninstall {
  [CmdletBinding(SupportsShouldProcess)]
  param(
    [Parameter(Mandatory)]$Manifest,
    [Parameter(Mandatory)][string]$Destination
  )
  $plan = Get-UninstallPlan $Manifest $Destination
  Invoke-PayloadUninstall -Plan $plan -Destination $Destination
  Write-Output "Hibiki $($Manifest.product_version) payload uninstall completed. User data preserved."
}

$manifest = Read-ReleaseManifest $ManifestPath

if ($Uninstall) {
  Test-ManifestFiles $manifest $PackageRoot | Out-Null
  Write-Output "Verified source tag $($manifest.source_tag), commit $($manifest.source_commit)."
  $destination = Resolve-HibikiDestination $DestinationPath
  $uninstallPlan = Get-UninstallPlan $manifest $destination
  $preservedPaths = Get-PreservedDataPaths
  Write-Output "Destination: $destination"
  Write-Output "Planned payload removals:"
  foreach ($item in $uninstallPlan) {
    $status = if ($item.Exists) { '(present)' } else { '(not installed)' }
    Write-Output "  $($item.RelativePath) $status"
  }
  Write-Output "Preserved data paths:"
  foreach ($p in $preservedPaths) {
    $status = if ($p.Exists) { '(exists)' } else { '(not found)' }
    Write-Output "  $($p.Name) $status"
  }
  if (-not $Apply) {
    Write-Output 'Uninstall dry-run only. No files were deleted.'
    Write-Output 'Re-run with -Apply -Uninstall after reviewing the plan.'
    return
  }
  if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
      [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Administrator privileges are required for -Apply -Uninstall.'
  }
  Invoke-HibikiUninstall $manifest $destination
} else {
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
}
