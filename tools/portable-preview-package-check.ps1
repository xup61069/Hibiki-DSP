#Requires -Version 7
[CmdletBinding(DefaultParameterSetName = 'Package')]
param(
  [Parameter(Mandatory, ParameterSetName = 'Package')]
  [string]$PackageRoot,
  [Parameter(Mandatory, ParameterSetName = 'Archive')]
  [string]$ArchivePath,
  [Parameter(Mandatory, ParameterSetName = 'Archive')]
  [string]$ExtractTo,
  [Parameter(ParameterSetName = 'Package')]
  [Parameter(ParameterSetName = 'Archive')]
  [switch]$LaunchSmoke,
  [Parameter(Mandatory, ParameterSetName = 'SelfTest')]
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$script:ManifestName = 'hibiki-portable-preview-manifest-v1.json'
$script:SchemaPath = Join-Path $repo 'schemas/portable-preview-package-manifest-v1.schema.json'
$script:PayloadLimitBytes = 512MB
$script:ArchiveLimitBytes = 768MB
$script:ArchiveUncompressedLimitBytes = 1GB
$script:ArchiveEntryLimit = 2048
$script:PayloadFileLimit = 1024
$script:ArchiveCompressionRatioLimit = 200.0
$script:BlockedExtensions = @(
  '.sys', '.inf', '.cat', '.msi', '.msix', '.appx', '.appxbundle', '.cab', '.vst3',
  '.ps1', '.cmd', '.bat', '.reg', '.cer', '.crt', '.pfx', '.pem', '.key', '.sig', '.pdb'
)

function Assert-ExactKeys {
  param([Parameter(Mandatory)][System.Collections.IDictionary]$Object, [Parameter(Mandatory)][string[]]$Allowed, [Parameter(Mandatory)][string]$Context)
  $actual = @($Object.Keys | ForEach-Object { [string]$_ } | Sort-Object)
  $expected = @($Allowed | Sort-Object)
  if ($actual.Count -ne $expected.Count -or @($actual | Where-Object { $_ -notin $expected }).Count -ne 0) {
    throw "$Context has unexpected or missing properties: $($actual -join ', ')"
  }
}

function Assert-PrintableString {
  param([Parameter(Mandatory)]$Value, [Parameter(Mandatory)][string]$Context, [int]$Maximum = 260)
  if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value) -or $Value.Length -gt $Maximum -or
      $Value -match '[\x00-\x1F\x7F-\x9F]') {
    throw "$Context must be a non-empty printable string within $Maximum characters."
  }
}

function Assert-LowerHex {
  param([Parameter(Mandatory)]$Value, [Parameter(Mandatory)][int]$Length, [Parameter(Mandatory)][string]$Context)
  if ($Value -isnot [string] -or $Value -notmatch "^[0-9a-f]{$Length}$") {
    throw "$Context must be $Length lowercase hexadecimal characters."
  }
}

function Assert-SafeRelativePath {
  param([Parameter(Mandatory)]$Value, [Parameter(Mandatory)][string]$Context)
  Assert-PrintableString -Value $Value -Context $Context -Maximum 260
  $path = [string]$Value
  if ($path -ne $path.Trim() -or [IO.Path]::IsPathRooted($path) -or $path.StartsWith('/') -or
      $path.EndsWith('/') -or $path.IndexOfAny([char[]]'<>:"\|?*') -ge 0) {
    throw "$Context is not a canonical relative package path: $path"
  }
  foreach ($segment in @($path -split '/')) {
    if ([string]::IsNullOrWhiteSpace($segment) -or $segment -in @('.', '..') -or
        $segment.StartsWith('.') -or $segment.EndsWith('.') -or $segment.EndsWith(' ')) {
      throw "$Context has an unsafe path segment: $path"
    }
    $stem = ($segment -split '\.', 2)[0]
    if ($stem -match '^(?i:con|prn|aux|nul|com[1-9]|lpt[1-9])$') {
      throw "$Context has a Windows reserved path segment: $path"
    }
  }
}

function Assert-AllowedPayloadPath {
  param([Parameter(Mandatory)]$Value, [Parameter(Mandatory)][string]$Context)
  Assert-SafeRelativePath -Value $Value -Context $Context
  $extension = [IO.Path]::GetExtension([string]$Value).ToLowerInvariant()
  if ($script:BlockedExtensions -contains $extension) {
    throw "$Context has a prohibited payload extension: $Value"
  }
}

function Assert-NoReparsePoint {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "$Context must not be a reparse point: $Path"
  }
  return $item
}

function Assert-NoDuplicateJsonProperties {
  param([Parameter(Mandatory)][System.Text.Json.JsonElement]$Element, [string]$Context = '$')
  if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($property in $Element.EnumerateObject()) {
      if (-not $seen.Add($property.Name)) { throw "Duplicate JSON property at ${Context}: $($property.Name)" }
      Assert-NoDuplicateJsonProperties -Element $property.Value -Context "$Context.$($property.Name)"
    }
    return
  }
  if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
    $index = 0
    foreach ($item in $Element.EnumerateArray()) {
      Assert-NoDuplicateJsonProperties -Element $item -Context "${Context}[$index]"
      $index++
    }
  }
}

function Get-SafeRegularFiles {
  param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Context)
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-Path -LiteralPath $rootFull -PathType Container)) { throw "$Context root does not exist: $rootFull" }
  [void](Assert-NoReparsePoint -Path $rootFull -Context "$Context root")
  $todo = [Collections.Generic.Stack[string]]::new()
  $todo.Push($rootFull)
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $records = [Collections.Generic.List[object]]::new()
  $visited = 0
  while ($todo.Count -gt 0) {
    $directory = $todo.Pop()
    [void](Assert-NoReparsePoint -Path $directory -Context "$Context directory")
    foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
      $visited++
      if ($visited -gt $script:ArchiveEntryLimit) { throw "$Context contains too many filesystem entries." }
      [void](Assert-NoReparsePoint -Path $item.FullName -Context $Context)
      if ($item.PSIsContainer) { $todo.Push($item.FullName); continue }
      $relative = [IO.Path]::GetRelativePath($rootFull, $item.FullName).Replace('\', '/')
      Assert-AllowedPayloadPath -Value $relative -Context "$Context payload"
      if (-not $seen.Add($relative)) { throw "$Context has a duplicate payload path: $relative" }
      if ($item.Length -lt 1 -or $item.Length -gt $script:PayloadLimitBytes) {
        throw "$Context payload size is outside bounds: $relative"
      }
      [void]$records.Add([pscustomobject]@{
        path = $relative
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        size = [int64]$item.Length
        full_name = $item.FullName
      })
      if ($records.Count -gt ($script:PayloadFileLimit + 1)) { throw "$Context contains too many payload files." }
    }
  }
  $ordered = [Collections.Generic.List[object]]::new()
  foreach ($record in $records) {
    $index = 0
    while ($index -lt $ordered.Count -and [string]::CompareOrdinal($ordered[$index].path, $record.path) -lt 0) { $index++ }
    $ordered.Insert($index, $record)
  }
  return @($ordered)
}

function Read-PortableManifest {
  param([Parameter(Mandatory)][string]$Root)
  $manifestPath = Join-Path $Root $script:ManifestName
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Missing package manifest: $manifestPath" }
  $manifestItem = Assert-NoReparsePoint -Path $manifestPath -Context 'Package manifest'
  if ($manifestItem.Name -cne $script:ManifestName) { throw "Package manifest name must be exactly $script:ManifestName" }
  if ($manifestItem.Length -lt 1 -or $manifestItem.Length -gt 1MB) { throw "Package manifest size is outside bounds: $manifestPath" }
  $text = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8
  try {
    $document = [System.Text.Json.JsonDocument]::Parse($text)
    try { Assert-NoDuplicateJsonProperties -Element $document.RootElement } finally { $document.Dispose() }
  } catch { throw "Package manifest JSON is invalid or ambiguous: $($_.Exception.Message)" }
  try {
    if (-not (Test-Json -Json $text -SchemaFile $script:SchemaPath -ErrorAction Stop)) { throw 'schema returned false' }
  } catch { throw "Package manifest schema validation failed: $($_.Exception.Message)" }
  try { return ($text | ConvertFrom-Json -AsHashtable -ErrorAction Stop) }
  catch { throw "Package manifest cannot be parsed: $($_.Exception.Message)" }
}

function Assert-PortableManifest {
  param([Parameter(Mandatory)][System.Collections.IDictionary]$Manifest, [Parameter(Mandatory)][object[]]$ActualRecords)
  Assert-ExactKeys -Object $Manifest -Allowed @(
    'schema_version', 'package_kind', 'product_version', 'source_tag', 'tag_commit', 'source_commit',
    'source_manifest_sha256', 'packager_commit', 'distribution_id', 'platform', 'entry_point', 'runtime',
    'driver', 'installer', 'limitations', 'files'
  ) -Context 'Package manifest'
  if ($Manifest.schema_version -ne 1 -or $Manifest.package_kind -ne 'portable-user-space-preview') {
    throw 'Package manifest has an unsupported schema or package kind.'
  }
  Assert-PrintableString -Value $Manifest.product_version -Context 'product_version' -Maximum 64
  if ($Manifest.product_version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+[A-Za-z0-9._:+-]{0,32}$') { throw 'product_version is invalid.' }
  if ($Manifest.source_tag -isnot [string] -or $Manifest.source_tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+[A-Za-z0-9._:+-]{0,32}$') {
    throw 'source_tag is invalid.'
  }
  if ($Manifest.product_version -cne $Manifest.source_tag.Substring(1)) { throw 'product_version does not match source_tag.' }
  Assert-LowerHex -Value $Manifest.tag_commit -Length 40 -Context 'tag_commit'
  Assert-LowerHex -Value $Manifest.source_commit -Length 40 -Context 'source_commit'
  Assert-LowerHex -Value $Manifest.source_manifest_sha256 -Length 64 -Context 'source_manifest_sha256'
  Assert-LowerHex -Value $Manifest.packager_commit -Length 40 -Context 'packager_commit'
  Assert-PrintableString -Value $Manifest.distribution_id -Context 'distribution_id' -Maximum 120
  if ($Manifest.platform -isnot [System.Collections.IDictionary]) { throw 'platform must be an object.' }
  Assert-ExactKeys -Object $Manifest.platform -Allowed @('target', 'os_min_build', 'architecture') -Context 'platform'
  if ($Manifest.platform.target -ne 'windows-11' -or $Manifest.platform.os_min_build -ne 26100 -or
      $Manifest.platform.architecture -ne 'x64') { throw 'platform does not match the portable-preview contract.' }
  if ($Manifest.entry_point -ne 'Hibiki.DesktopPreview.exe' -or $Manifest.runtime -ne 'self-contained-dotnet-win-x64' -or
      $Manifest.driver -ne 'not-included' -or $Manifest.installer -ne 'not-included') {
    throw 'Package manifest has an invalid entry point, runtime, driver, or installer declaration.'
  }
  $limitations = @($Manifest.limitations)
  if ($limitations.Count -lt 1 -or $limitations.Count -gt 16) { throw 'limitations count is outside bounds.' }
  foreach ($limitation in $limitations) { Assert-PrintableString -Value $limitation -Context 'limitation' -Maximum 240 }
  $declared = @($Manifest.files)
  if ($declared.Count -lt 3 -or $declared.Count -gt $script:PayloadFileLimit) { throw 'files count is outside bounds.' }
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $exactNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  $previous = $null
  for ($index = 0; $index -lt $declared.Count; $index++) {
    $entry = $declared[$index]
    if ($entry -isnot [System.Collections.IDictionary]) { throw "files[$index] must be an object." }
    Assert-ExactKeys -Object $entry -Allowed @('path', 'sha256', 'size') -Context "files[$index]"
    Assert-AllowedPayloadPath -Value $entry.path -Context "files[$index].path"
    if (-not $seen.Add([string]$entry.path)) { throw "files[$index].path is duplicated." }
    [void]$exactNames.Add([string]$entry.path)
    if ($null -ne $previous -and [string]::CompareOrdinal([string]$previous, [string]$entry.path) -ge 0) {
      throw 'files must be ordinal-sorted by path.'
    }
    $previous = [string]$entry.path
    Assert-LowerHex -Value $entry.sha256 -Length 64 -Context "files[$index].sha256"
    if (($entry.size -isnot [int] -and $entry.size -isnot [long]) -or [int64]$entry.size -lt 1 -or
        [int64]$entry.size -gt $script:PayloadLimitBytes) { throw "files[$index].size is outside bounds." }
  }
  foreach ($requiredPayload in @($Manifest.entry_point, 'PORTABLE_PREVIEW_README.txt', 'THIRD_PARTY.yml')) {
    if (-not $exactNames.Contains($requiredPayload)) { throw "Package manifest is missing required payload: $requiredPayload" }
  }
  if ($declared.Count -ne $ActualRecords.Count) { throw 'Package payload file set does not match manifest.' }
  for ($index = 0; $index -lt $declared.Count; $index++) {
    $expected = $declared[$index]
    $actual = $ActualRecords[$index]
    if ($expected.path -cne $actual.path -or $expected.sha256 -cne $actual.sha256 -or [int64]$expected.size -ne [int64]$actual.size) {
      throw "Package payload mismatch at index $index."
    }
  }
}

function Test-PortablePackageRoot {
  param([Parameter(Mandatory)][string]$Root)
  if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Package root does not exist: $Root" }
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  [void](Assert-NoReparsePoint -Path $rootFull -Context 'Package root')
  $manifest = Read-PortableManifest -Root $rootFull
  $actual = @(Get-SafeRegularFiles -Root $rootFull -Context 'Package root' |
    Where-Object { $_.path -cne $script:ManifestName })
  Assert-PortableManifest -Manifest $manifest -ActualRecords $actual
  $entryPoint = Join-Path $rootFull $manifest.entry_point
  if (-not (Test-Path -LiteralPath $entryPoint -PathType Leaf)) { throw 'Package entry point is missing.' }
  [void](Assert-NoReparsePoint -Path $entryPoint -Context 'Package entry point')
  return [pscustomobject]@{
    package_root = $rootFull
    entry_point = $entryPoint
    source_tag = $manifest.source_tag
    tag_commit = $manifest.tag_commit
    source_commit = $manifest.source_commit
    source_manifest_sha256 = $manifest.source_manifest_sha256
    packager_commit = $manifest.packager_commit
    payload_files = $actual.Count
  }
}

function Read-ArchiveSidecar {
  param([Parameter(Mandatory)][string]$Archive)
  $sidecar = "$Archive.sha256"
  if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) { throw "Missing archive SHA-256 sidecar: $sidecar" }
  $sidecarItem = Assert-NoReparsePoint -Path $sidecar -Context 'Archive sidecar'
  if ($sidecarItem.Length -lt 1 -or $sidecarItem.Length -gt 512) { throw 'Archive SHA-256 sidecar size is outside bounds.' }
  $raw = Get-Content -LiteralPath $sidecar -Raw -Encoding utf8
  if ($raw -notmatch '^[^\r\n]+(?:\r?\n)?$') { throw 'Archive SHA-256 sidecar must contain exactly one line.' }
  $line = $raw.TrimEnd([char[]]"`r`n")
  $name = [IO.Path]::GetFileName($Archive)
  $match = [regex]::Match($line, '^([0-9a-f]{64}) \*' + [regex]::Escape($name) + '$')
  if (-not $match.Success) { throw 'Archive SHA-256 sidecar has an invalid format or filename.' }
  $actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -cne $match.Groups[1].Value) { throw 'Archive SHA-256 sidecar does not match the ZIP bytes.' }
}

function Assert-SafeExtractTarget {
  param([Parameter(Mandatory)][string]$Path)
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (-not [IO.Path]::IsPathFullyQualified($full) -or $full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\', '/')) {
    throw "Extraction target is not a safe absolute directory: $Path"
  }
  if (Test-Path -LiteralPath $full) { throw "Extraction target already exists: $full" }
  $parent = Split-Path -Parent $full
  while ($parent) {
    if (Test-Path -LiteralPath $parent) { [void](Assert-NoReparsePoint -Path $parent -Context 'Extraction target parent'); break }
    $next = Split-Path -Parent $parent
    if ($next -eq $parent) { break }
    $parent = $next
  }
  return $full
}

function Assert-PathContainedBy {
  param([Parameter(Mandatory)][string]$Candidate, [Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Context)
  $candidateFull = [IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not $candidateFull.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Context escapes extraction root."
  }
  return $candidateFull
}

function Test-PortablePackageArchive {
  param([Parameter(Mandatory)][string]$Archive, [Parameter(Mandatory)][string]$Destination)
  if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) { throw "Archive does not exist: $Archive" }
  [void](Assert-NoReparsePoint -Path $Archive -Context 'Archive')
  $archiveItem = Get-Item -LiteralPath $Archive -Force -ErrorAction Stop
  if ($archiveItem.Extension -cne '.zip' -or $archiveItem.Length -lt 1 -or $archiveItem.Length -gt $script:ArchiveLimitBytes) {
    throw 'Archive type or size is outside bounds.'
  }
  Read-ArchiveSidecar -Archive $Archive
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $target = Assert-SafeExtractTarget -Path $Destination
  $zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
  $createdTarget = $false
  try {
    if ($zip.Entries.Count -lt 1 -or $zip.Entries.Count -gt $script:ArchiveEntryLimit) { throw 'ZIP entry count is outside bounds.' }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $fileEntries = [Collections.Generic.List[object]]::new()
    $totalUncompressed = [int64]0
    $manifestExact = $false
    foreach ($entry in $zip.Entries) {
      $name = [string]$entry.FullName
      if ([string]::IsNullOrWhiteSpace($name) -or $name.Contains('\\') -or $name -match '//') { throw "ZIP has an unsafe entry name: $name" }
      $isDirectory = $name.EndsWith('/')
      $safeName = if ($isDirectory) { $name.TrimEnd('/') } else { $name }
      if ([string]::IsNullOrWhiteSpace($safeName)) { throw "ZIP has an empty directory entry: $name" }
      Assert-SafeRelativePath -Value $safeName -Context 'ZIP entry'
      if (-not $names.Add($safeName)) { throw "ZIP has a duplicate entry: $name" }
      if (($entry.ExternalAttributes -shr 16 -band 0xF000) -eq 0xA000) { throw "ZIP entry is a symlink: $name" }
      if ($name -ceq $script:ManifestName) { $manifestExact = $true }
      if ($isDirectory) { continue }
      Assert-AllowedPayloadPath -Value $safeName -Context 'ZIP entry'
      if ($entry.Length -lt 1 -or $entry.Length -gt $script:PayloadLimitBytes -or $entry.CompressedLength -lt 1) {
        throw "ZIP entry size is outside bounds: $name"
      }
      if (($entry.Length / [double]$entry.CompressedLength) -gt $script:ArchiveCompressionRatioLimit) {
        throw "ZIP entry compression ratio is outside bounds: $name"
      }
      if ($totalUncompressed -gt ($script:ArchiveUncompressedLimitBytes - [int64]$entry.Length)) { throw 'ZIP total uncompressed size is outside bounds.' }
      $totalUncompressed += [int64]$entry.Length
      [void]$fileEntries.Add($entry)
    }
    if (-not $manifestExact) { throw 'ZIP is missing the exact package manifest entry.' }
    [IO.Directory]::CreateDirectory($target) | Out-Null
    $createdTarget = $true
    [void](Assert-NoReparsePoint -Path $target -Context 'Extraction target')
    $writtenTotal = [int64]0
    foreach ($entry in $fileEntries) {
      $relative = $entry.FullName.Replace('/', [IO.Path]::DirectorySeparatorChar)
      $destinationPath = Assert-PathContainedBy -Candidate (Join-Path $target $relative) -Root $target -Context "ZIP entry $($entry.FullName)"
      $parent = Split-Path -Parent $destinationPath
      [IO.Directory]::CreateDirectory($parent) | Out-Null
      [void](Assert-NoReparsePoint -Path $parent -Context 'Extraction directory')
      $input = $entry.Open()
      try {
        $output = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
          $buffer = [byte[]]::new(81920)
          $written = [int64]0
          while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
            if ($written -gt ([int64]$entry.Length - $read) -or $writtenTotal -gt ($script:ArchiveUncompressedLimitBytes - $read)) {
              throw "ZIP entry exceeds preflight size during extraction: $($entry.FullName)"
            }
            $output.Write($buffer, 0, $read)
            $written += $read
            $writtenTotal += $read
          }
          if ($written -ne [int64]$entry.Length) { throw "ZIP entry size changed during extraction: $($entry.FullName)" }
        } finally { $output.Dispose() }
      } finally { $input.Dispose() }
    }
    if ($writtenTotal -ne $totalUncompressed) { throw 'ZIP total size changed during extraction.' }
    return (Test-PortablePackageRoot -Root $target)
  } catch {
    $failure = $_
    if ($createdTarget -and (Test-Path -LiteralPath $target)) {
      try {
        [void](Assert-NoReparsePoint -Path $target -Context 'Failed extraction target')
        Remove-Item -LiteralPath $target -Recurse -Force
      } catch { Write-Warning "Failed to clean partial extraction target: $target" }
    }
    throw $failure
  } finally { $zip.Dispose() }
}

function Invoke-PortablePreviewLaunchSmoke {
  param([Parameter(Mandatory)][string]$EntryPoint)
  if (-not (Test-Path -LiteralPath $EntryPoint -PathType Leaf)) { throw "Package entry point is missing: $EntryPoint" }
  [void](Assert-NoReparsePoint -Path $EntryPoint -Context 'Package entry point')
  $process = Start-Process -FilePath $EntryPoint -WorkingDirectory (Split-Path -Parent $EntryPoint) -WindowStyle Hidden -PassThru
  try {
    Start-Sleep -Seconds 3
    $process.Refresh()
    if ($process.HasExited) { throw "Portable preview exited during launch smoke: $($process.ExitCode)" }
  } finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
    $process.WaitForExit()
  }
}

function New-SelfTestManifest {
  param([Parameter(Mandatory)][string]$Root)
  $records = @(Get-SafeRegularFiles -Root $Root -Context 'Self-test package' |
    Where-Object { $_.path -cne $script:ManifestName } |
    ForEach-Object { [ordered]@{ path = $_.path; sha256 = $_.sha256; size = $_.size } })
  return [ordered]@{
    schema_version = 1
    package_kind = 'portable-user-space-preview'
    product_version = '1.0.0'
    source_tag = 'v1.0.0'
    tag_commit = ('a' * 40)
    source_commit = ('b' * 40)
    source_manifest_sha256 = ('c' * 64)
    packager_commit = ('d' * 40)
    distribution_id = 'hibiki-public-2026'
    platform = [ordered]@{ target = 'windows-11'; os_min_build = 26100; architecture = 'x64' }
    entry_point = 'Hibiki.DesktopPreview.exe'
    runtime = 'self-contained-dotnet-win-x64'
    driver = 'not-included'
    installer = 'not-included'
    limitations = @('Driver-free preview only.')
    files = $records
  }
}

function Remove-OwnedSelfTestPath {
  param([Parameter(Mandatory)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return }
  $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (-not $full.StartsWith($temp + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
      -not ([IO.Path]::GetFileName($full).StartsWith('hibiki-portable-package-', [StringComparison]::Ordinal))) {
    throw "Refusing to remove unexpected self-test path: $full"
  }
  [void](Assert-NoReparsePoint -Path $full -Context 'Self-test path')
  Remove-Item -LiteralPath $full -Recurse -Force
}

function Write-SelfTestSidecar {
  param([Parameter(Mandatory)][string]$Archive)
  $hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$Archive.sha256", "$hash *$([IO.Path]::GetFileName($Archive))", [Text.UTF8Encoding]::new($false))
}

function Invoke-PortablePackageSelfTest {
  $root = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-portable-package-' + [guid]::NewGuid().ToString('N'))
  $extract = $root + '-extract'
  $archive = $root + '.zip'
  $unsafeArchive = $root + '-unsafe.zip'
  try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $root 'Hibiki.DesktopPreview.exe'), [byte[]](1, 2, 3, 4))
    [IO.File]::WriteAllText((Join-Path $root 'PORTABLE_PREVIEW_README.txt'), 'preview', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'THIRD_PARTY.yml'), 'dependencies: []', [Text.UTF8Encoding]::new($false))
    $manifestPath = Join-Path $root $script:ManifestName
    [IO.File]::WriteAllText($manifestPath, ((New-SelfTestManifest -Root $root) | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    [void](Test-PortablePackageRoot -Root $root)
    $caseCount = 1

    [IO.File]::WriteAllText((Join-Path $root 'extra.txt'), 'unexpected', [Text.UTF8Encoding]::new($false))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected an undeclared-file rejection.' }
    Remove-Item -LiteralPath (Join-Path $root 'extra.txt') -Force
    $caseCount++

    [IO.File]::WriteAllBytes((Join-Path $root 'debug.pdb'), [byte[]](1))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a debug-symbol rejection.' }
    Remove-Item -LiteralPath (Join-Path $root 'debug.pdb') -Force
    $caseCount++

    $bad = Get-Content -LiteralPath $manifestPath -Raw
    $bad = $bad -replace '"schema_version": 1', '"schema_version": 1, "schema_version": 1'
    [IO.File]::WriteAllText($manifestPath, $bad, [Text.UTF8Encoding]::new($false))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a duplicate-property rejection.' }
    [IO.File]::WriteAllText($manifestPath, ((New-SelfTestManifest -Root $root) | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $caseCount++

    [IO.Compression.ZipFile]::CreateFromDirectory($root, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-SelfTestSidecar -Archive $archive
    [void](Test-PortablePackageArchive -Archive $archive -Destination $extract)
    $caseCount++

    [IO.File]::WriteAllText("$archive.sha256", "$('0' * 64) *$([IO.Path]::GetFileName($archive))", [Text.UTF8Encoding]::new($false))
    $caught = $false
    try { Read-ArchiveSidecar -Archive $archive } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a bad-sidecar rejection.' }
    $caseCount++

    $unsafe = [IO.Compression.ZipFile]::Open($unsafeArchive, [IO.Compression.ZipArchiveMode]::Create)
    try { [void]$unsafe.CreateEntry('../escape.txt') } finally { $unsafe.Dispose() }
    Write-SelfTestSidecar -Archive $unsafeArchive
    $caught = $false
    try { [void](Test-PortablePackageArchive -Archive $unsafeArchive -Destination ($root + '-unsafe-extract')) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected an unsafe-ZIP-path rejection.' }
    $caseCount++
    Write-Output "Portable preview package check self-test passed ($caseCount cases)."
  } finally {
    foreach ($path in @($root, $extract, ($root + '-unsafe-extract'))) { Remove-OwnedSelfTestPath -Path $path }
    foreach ($file in @($archive, "$archive.sha256", $unsafeArchive, "$unsafeArchive.sha256")) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force } }
  }
}

if ($SelfTest) {
  Invoke-PortablePackageSelfTest
  exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Archive') {
  $result = Test-PortablePackageArchive -Archive $ArchivePath -Destination $ExtractTo
} else {
  $result = Test-PortablePackageRoot -Root $PackageRoot
}
if ($LaunchSmoke) { Invoke-PortablePreviewLaunchSmoke -EntryPoint $result.entry_point }
Write-Output "Portable preview package check passed: source_tag=$($result.source_tag) payload_files=$($result.payload_files) entry_point=$($result.entry_point)"
