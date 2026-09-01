#Requires -Version 7
[CmdletBinding(DefaultParameterSetName = 'Package')]
param(
  [Parameter(Mandatory, ParameterSetName = 'Package')]
  [string]$PackageRoot,
  [Parameter(Mandatory, ParameterSetName = 'Package')]
  [Parameter(Mandatory, ParameterSetName = 'Archive')]
  [string]$SourceRepository,
  [Parameter(Mandatory, ParameterSetName = 'Archive')]
  [string]$ArchivePath,
  [Parameter(Mandatory, ParameterSetName = 'Archive')]
  [string]$ExtractTo,
  [Parameter(ParameterSetName = 'Archive')]
  [ValidatePattern('^[0-9a-f]{64}$')]
  [string]$ExpectedArchiveSha256,
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
$script:ArchiveCentralDirectoryLimitBytes = 4MB
$script:SourceManifestRoot = 'release/manifests'
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
    if ($stem -match '^(?i:con|prn|aux|nul|com(?:[1-9]|[¹²³])|lpt(?:[1-9]|[¹²³]))$') {
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

function Get-CanonicalFullPath {
  param([Parameter(Mandatory)][string]$Path)
  $full = [IO.Path]::GetFullPath($Path)
  $root = [IO.Path]::GetPathRoot($full)
  if ($full.Length -gt $root.Length) { return $full.TrimEnd([char[]]@('\', '/')) }
  return $full
}

function Assert-NoReparseAncestors {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  $current = Get-CanonicalFullPath -Path $Path
  while ($true) {
    if (Test-Path -LiteralPath $current) {
      [void](Assert-NoReparsePoint -Path $current -Context $Context)
    }
    $parentInfo = [IO.Directory]::GetParent($current)
    if ($null -eq $parentInfo) { break }
    $parent = Get-CanonicalFullPath -Path $parentInfo.FullName
    if ($parent -ceq $current) { break }
    $current = $parent
  }
}

function Assert-NoReparseTree {
  param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Context)
  Assert-NoReparseAncestors -Path $Root -Context $Context
  $todo = [Collections.Generic.Stack[string]]::new()
  $todo.Push((Get-CanonicalFullPath -Path $Root))
  while ($todo.Count -gt 0) {
    $directory = $todo.Pop()
    [void](Assert-NoReparsePoint -Path $directory -Context $Context)
    foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
      [void](Assert-NoReparsePoint -Path $item.FullName -Context $Context)
      if ($item.PSIsContainer) { $todo.Push($item.FullName) }
    }
  }
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
  $rootFull = Get-CanonicalFullPath -Path $Root
  if (-not (Test-Path -LiteralPath $rootFull -PathType Container)) { throw "$Context root does not exist: $rootFull" }
  Assert-NoReparseAncestors -Path $rootFull -Context "$Context root"
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

function Invoke-GitText {
  param([Parameter(Mandatory)][string]$Repository, [Parameter(Mandatory)][string[]]$Arguments, [Parameter(Mandatory)][string]$FailurePrefix)
  $output = @(& git -C $Repository @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "${FailurePrefix}: $($output -join [Environment]::NewLine)" }
  return ($output -join "`n").Trim()
}

function Get-GitBlobBytes {
  param([Parameter(Mandatory)][string]$Repository, [Parameter(Mandatory)][string]$BlobSha)
  if ($BlobSha -notmatch '^[0-9a-f]{40}$') { throw "Git blob identifier is invalid: $BlobSha" }
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = 'git'
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  foreach ($argument in @('-C', $Repository, 'cat-file', 'blob', $BlobSha)) { [void]$psi.ArgumentList.Add($argument) }
  $process = $null
  $memory = [IO.MemoryStream]::new()
  try {
    $process = [Diagnostics.Process]::Start($psi)
    if ($null -eq $process) { throw "Cannot start git cat-file for $BlobSha" }
    $process.StandardOutput.BaseStream.CopyTo($memory)
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Cannot read Git blob $BlobSha; $($stderr.Trim())" }
    return ,$memory.ToArray()
  } finally {
    $memory.Dispose()
    if ($null -ne $process) { $process.Dispose() }
  }
}

function Get-Sha256Hex {
  param([Parameter(Mandatory)][byte[]]$Bytes)
  $hash = [Security.Cryptography.SHA256]::Create()
  try { return ([Convert]::ToHexString($hash.ComputeHash($Bytes))).ToLowerInvariant() }
  finally { $hash.Dispose() }
}

function Get-SourceManifestBinding {
  param([Parameter(Mandatory)][System.Collections.IDictionary]$Manifest, [Parameter(Mandatory)][string]$Repository)
  $sourceRoot = Get-CanonicalFullPath -Path $Repository
  if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) { throw "Source repository does not exist: $sourceRoot" }
  Assert-NoReparseAncestors -Path $sourceRoot -Context 'Source repository'
  if ((Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', '--is-inside-work-tree') -FailurePrefix 'Source repository check failed') -cne 'true') {
    throw 'Source repository is not a Git worktree.'
  }
  $status = Invoke-GitText -Repository $sourceRoot -Arguments @('status', '--porcelain', '--untracked-files=all') -FailurePrefix 'Source repository status check failed'
  if ($status) { throw 'Source repository must be clean before source-binding validation.' }
  $branch = Invoke-GitText -Repository $sourceRoot -Arguments @('branch', '--show-current') -FailurePrefix 'Source checkout mode check failed'
  if ($branch) { throw "Source repository must be a detached checkout, not branch '$branch'." }

  $tagRef = 'refs/tags/' + [string]$Manifest.source_tag
  if ((Invoke-GitText -Repository $sourceRoot -Arguments @('cat-file', '-t', $tagRef) -FailurePrefix 'Source tag lookup failed') -cne 'tag') {
    throw "Source tag must be annotated: $($Manifest.source_tag)"
  }
  $tagCommit = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', ($tagRef + '^{}')) -FailurePrefix 'Source tag target lookup failed'
  $head = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Source HEAD lookup failed'
  if ($head -cne $tagCommit) { throw "Source repository HEAD must be the detached tag target $tagCommit, got $head" }

  $provenanceScript = Join-Path $sourceRoot 'tools/release-provenance-check.ps1'
  if (-not (Test-Path -LiteralPath $provenanceScript -PathType Leaf)) { throw 'Source-tag release provenance checker is missing.' }
  Assert-NoReparseAncestors -Path $provenanceScript -Context 'Source-tag release provenance checker'
  $provenanceOutput = @(& pwsh -NoProfile -File $provenanceScript -Tag $Manifest.source_tag -Repository $sourceRoot 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "Source-tag provenance gate failed: $($provenanceOutput -join [Environment]::NewLine)" }

  $parentLine = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-list', '--parents', '-n', '1', $tagCommit) -FailurePrefix 'Source parent lookup failed'
  $parents = @($parentLine -split '\s+' | Where-Object { $_ })
  if ($parents.Count -ne 2 -or $parents[0] -cne $tagCommit -or $parents[1] -notmatch '^[0-9a-f]{40}$') {
    throw 'Source tag target must be a single-parent provenance metadata commit.'
  }
  $sourceCommit = $parents[1]
  $manifestPath = $script:SourceManifestRoot + '/' + [string]$Manifest.source_tag + '.json'
  $treeLine = Invoke-GitText -Repository $sourceRoot -Arguments @('ls-tree', $tagCommit, '--', $manifestPath) -FailurePrefix 'Source manifest lookup failed'
  $treePattern = '^100644 blob (?<blob>[0-9a-f]{40})' + [char]9 + [regex]::Escape($manifestPath) + '$'
  $treeMatch = [regex]::Match($treeLine, $treePattern)
  if (-not $treeMatch.Success) { throw "Source manifest must be a regular 100644 blob: $manifestPath" }
  $rawManifest = Get-GitBlobBytes -Repository $sourceRoot -BlobSha $treeMatch.Groups['blob'].Value
  if ($rawManifest.Length -lt 1 -or $rawManifest.Length -gt 1MB) { throw 'Source manifest size is outside bounds.' }
  try { $sourceManifestText = [Text.UTF8Encoding]::new($false, $true).GetString($rawManifest) }
  catch { throw "Source manifest must be UTF-8: $($_.Exception.Message)" }
  try { $sourceManifest = $sourceManifestText | ConvertFrom-Json -AsHashtable -ErrorAction Stop }
  catch { throw "Source manifest cannot be parsed: $($_.Exception.Message)" }
  if ($sourceManifest -isnot [System.Collections.IDictionary]) { throw 'Source manifest must be a JSON object.' }
  foreach ($name in @('source_tag', 'product_version', 'source_commit', 'distribution_id')) {
    if (-not $sourceManifest.ContainsKey($name) -or $sourceManifest[$name] -isnot [string]) { throw "Source manifest is missing string '$name'." }
  }
  return [pscustomobject]@{
    source_root = $sourceRoot
    source_tag = [string]$Manifest.source_tag
    tag_commit = $tagCommit
    source_commit = $sourceCommit
    source_manifest_sha256 = Get-Sha256Hex -Bytes $rawManifest
    product_version = [string]$sourceManifest.product_version
    distribution_id = [string]$sourceManifest.distribution_id
    manifest_source_tag = [string]$sourceManifest.source_tag
    manifest_source_commit = [string]$sourceManifest.source_commit
  }
}

function Assert-PortableSourceBinding {
  param([Parameter(Mandatory)][System.Collections.IDictionary]$Manifest, [Parameter(Mandatory)][string]$Repository)
  $binding = Get-SourceManifestBinding -Manifest $Manifest -Repository $Repository
  if ($Manifest.tag_commit -cne $binding.tag_commit -or $Manifest.source_commit -cne $binding.source_commit -or
      $Manifest.source_manifest_sha256 -cne $binding.source_manifest_sha256 -or $Manifest.distribution_id -cne $binding.distribution_id -or
      $Manifest.product_version -cne $binding.product_version -or $binding.manifest_source_tag -cne $Manifest.source_tag -or
      $binding.manifest_source_commit -cne $Manifest.source_commit) {
    throw 'Package manifest provenance does not match the authoritative detached source tag.'
  }
  return $binding
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
  param(
    [Parameter(Mandatory)][string]$Root,
    [string]$SourceRepository,
    [switch]$SkipSourceBinding
  )
  if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Package root does not exist: $Root" }
  $rootFull = Get-CanonicalFullPath -Path $Root
  Assert-NoReparseAncestors -Path $rootFull -Context 'Package root'
  $manifest = Read-PortableManifest -Root $rootFull
  $actual = @(Get-SafeRegularFiles -Root $rootFull -Context 'Package root' |
    Where-Object { $_.path -cne $script:ManifestName })
  Assert-PortableManifest -Manifest $manifest -ActualRecords $actual
  $binding = $null
  if (-not $SkipSourceBinding) {
    if ([string]::IsNullOrWhiteSpace($SourceRepository)) { throw 'SourceRepository is required for package provenance validation.' }
    $binding = Assert-PortableSourceBinding -Manifest $manifest -Repository $SourceRepository
  }
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
    distribution_id = $manifest.distribution_id
    source_repository = if ($null -ne $binding) { $binding.source_root } else { $null }
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
  return $actual
}

function Assert-SafeExtractTarget {
  param([Parameter(Mandatory)][string]$Path)
  if (-not [IO.Path]::IsPathFullyQualified($Path)) {
    throw "Extraction target is not a safe absolute directory: $Path"
  }
  $full = Get-CanonicalFullPath -Path $Path
  $root = [IO.Path]::GetPathRoot($full)
  if ($full -ceq $root) {
    throw "Extraction target is not a safe absolute directory: $Path"
  }
  if (Test-Path -LiteralPath $full) { throw "Extraction target already exists: $full" }
  Assert-NoReparseAncestors -Path $full -Context 'Extraction target parent'
  return $full
}

function Assert-PathContainedBy {
  param([Parameter(Mandatory)][string]$Candidate, [Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Context)
  $candidateFull = Get-CanonicalFullPath -Path $Candidate
  $rootFull = Get-CanonicalFullPath -Path $Root
  if (-not $candidateFull.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Context escapes extraction root."
  }
  return $candidateFull
}

function Assert-ZipCentralDirectoryPreflight {
  param([Parameter(Mandatory)][string]$Archive, [Parameter(Mandatory)][int64]$ArchiveLength)
  $tailLength = [int][Math]::Min($ArchiveLength, [int64](65535 + 22))
  $tail = [byte[]]::new($tailLength)
  $stream = [IO.File]::Open($Archive, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
  try {
    [void]$stream.Seek(-$tailLength, [IO.SeekOrigin]::End)
    $readTotal = 0
    while ($readTotal -lt $tailLength) {
      $read = $stream.Read($tail, $readTotal, $tailLength - $readTotal)
      if ($read -le 0) { throw 'Cannot read ZIP end-of-central-directory record.' }
      $readTotal += $read
    }
  } finally {
    $stream.Dispose()
  }
  $eocdIndex = -1
  for ($index = $tailLength - 22; $index -ge 0; $index--) {
    if ($tail[$index] -ne 0x50 -or $tail[$index + 1] -ne 0x4b -or $tail[$index + 2] -ne 0x05 -or $tail[$index + 3] -ne 0x06) {
      continue
    }
    $commentLength = [BitConverter]::ToUInt16($tail, $index + 20)
    if (($index + 22 + $commentLength) -eq $tailLength) { $eocdIndex = $index; break }
  }
  if ($eocdIndex -lt 0) { throw 'ZIP has no canonical end-of-central-directory record.' }
  $disk = [BitConverter]::ToUInt16($tail, $eocdIndex + 4)
  $centralDirectoryDisk = [BitConverter]::ToUInt16($tail, $eocdIndex + 6)
  $entriesOnDisk = [BitConverter]::ToUInt16($tail, $eocdIndex + 8)
  $entriesTotal = [BitConverter]::ToUInt16($tail, $eocdIndex + 10)
  $centralDirectorySize = [BitConverter]::ToUInt32($tail, $eocdIndex + 12)
  $centralDirectoryOffset = [BitConverter]::ToUInt32($tail, $eocdIndex + 16)
  if ($disk -ne 0 -or $centralDirectoryDisk -ne 0 -or $entriesOnDisk -ne $entriesTotal) {
    throw 'ZIP must be a single-disk archive.'
  }
  if ($entriesTotal -eq 0xffff -or $centralDirectorySize -eq 0xffffffff -or $centralDirectoryOffset -eq 0xffffffff) {
    throw 'ZIP64 archives are not permitted for the portable-preview package.'
  }
  if ($entriesTotal -lt 1 -or $entriesTotal -gt $script:ArchiveEntryLimit) {
    throw 'ZIP entry count is outside bounds.'
  }
  if ($centralDirectorySize -lt 1 -or $centralDirectorySize -gt $script:ArchiveCentralDirectoryLimitBytes) {
    throw 'ZIP central-directory size is outside bounds.'
  }
  $eocdOffset = $ArchiveLength - $tailLength + $eocdIndex
  if ([int64]$centralDirectoryOffset + [int64]$centralDirectorySize -gt $eocdOffset) {
    throw 'ZIP central-directory bounds overlap the end-of-central-directory record.'
  }
}

function Test-PortablePackageArchive {
  param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$Destination,
    [string]$SourceRepository,
    [string]$ExpectedArchiveSha256,
    [switch]$SkipSourceBinding
  )
  if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) { throw "Archive does not exist: $Archive" }
  $archiveFull = Get-CanonicalFullPath -Path $Archive
  Assert-NoReparseAncestors -Path $archiveFull -Context 'Archive'
  $archiveItem = Get-Item -LiteralPath $archiveFull -Force -ErrorAction Stop
  if ($archiveItem.Extension -cne '.zip' -or $archiveItem.Length -lt 1 -or $archiveItem.Length -gt $script:ArchiveLimitBytes) {
    throw 'Archive type or size is outside bounds.'
  }
  $archiveHash = Read-ArchiveSidecar -Archive $archiveFull
  if ($ExpectedArchiveSha256) {
    Assert-LowerHex -Value $ExpectedArchiveSha256 -Length 64 -Context 'ExpectedArchiveSha256'
    if ($archiveHash -cne $ExpectedArchiveSha256) { throw 'Archive SHA-256 does not match the independently expected release value.' }
  }
  Assert-ZipCentralDirectoryPreflight -Archive $archiveFull -ArchiveLength $archiveItem.Length
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $target = Assert-SafeExtractTarget -Path $Destination
  $zip = [IO.Compression.ZipFile]::OpenRead($archiveFull)
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
      $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
      $dosAttributes = $entry.ExternalAttributes -band 0xffff
      if (($dosAttributes -band 0x0400) -ne 0) { throw "ZIP entry is a DOS reparse point: $name" }
      if ($unixType -eq 0xA000) { throw "ZIP entry is a symlink: $name" }
      $allowedUnixTypes = if ($isDirectory) { @(0, 0x4000) } else { @(0, 0x8000) }
      if ($unixType -notin $allowedUnixTypes) { throw "ZIP entry is not a regular file or directory: $name" }
      if ($name -ceq $script:ManifestName) { $manifestExact = $true }
      if ($isDirectory) {
        if ($entry.Length -ne 0 -or $entry.CompressedLength -ne 0) { throw "ZIP directory entry carries data: $name" }
        continue
      }
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
    Assert-NoReparseAncestors -Path $target -Context 'Extraction target'
    $writtenTotal = [int64]0
    foreach ($entry in $fileEntries) {
      $relative = $entry.FullName.Replace('/', [IO.Path]::DirectorySeparatorChar)
      $destinationPath = Assert-PathContainedBy -Candidate (Join-Path $target $relative) -Root $target -Context "ZIP entry $($entry.FullName)"
      $parent = Split-Path -Parent $destinationPath
      [IO.Directory]::CreateDirectory($parent) | Out-Null
      Assert-NoReparseAncestors -Path $parent -Context 'Extraction directory'
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
    return (Test-PortablePackageRoot -Root $target -SourceRepository $SourceRepository -SkipSourceBinding:$SkipSourceBinding)
  } catch {
    $failure = $_
    if ($createdTarget -and (Test-Path -LiteralPath $target)) {
      try {
        Assert-NoReparseTree -Root $target -Context 'Failed extraction target'
        Remove-Item -LiteralPath $target -Recurse -Force
      } catch { Write-Warning "Failed to clean partial extraction target: $target" }
    }
    throw $failure
  } finally { $zip.Dispose() }
}

function Invoke-PortablePreviewLaunchSmoke {
  param([Parameter(Mandatory)][string]$EntryPoint)
  if (-not (Test-Path -LiteralPath $EntryPoint -PathType Leaf)) { throw "Package entry point is missing: $EntryPoint" }
  Assert-NoReparseAncestors -Path $EntryPoint -Context 'Package entry point'
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
  $directoryDataArchive = $root + '-directory-data.zip'
  $reparseArchive = $root + '-reparse.zip'
  try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $root 'Hibiki.DesktopPreview.exe'), [byte[]](1, 2, 3, 4))
    [IO.File]::WriteAllText((Join-Path $root 'PORTABLE_PREVIEW_README.txt'), 'preview', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $root 'THIRD_PARTY.yml'), 'dependencies: []', [Text.UTF8Encoding]::new($false))
    $manifestPath = Join-Path $root $script:ManifestName
    [IO.File]::WriteAllText($manifestPath, ((New-SelfTestManifest -Root $root) | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    [void](Test-PortablePackageRoot -Root $root -SkipSourceBinding)
    $caseCount = 1

    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a missing-source-binding rejection.' }
    $caseCount++

    $caught = $false
    try { Assert-SafeRelativePath -Value ('COM' + [char]0x00b9 + '.txt') -Context 'Self-test reserved Windows path' } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a superscript-COM reserved-name rejection.' }
    $caseCount++

    [IO.File]::WriteAllText((Join-Path $root 'extra.txt'), 'unexpected', [Text.UTF8Encoding]::new($false))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root -SkipSourceBinding) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected an undeclared-file rejection.' }
    Remove-Item -LiteralPath (Join-Path $root 'extra.txt') -Force
    $caseCount++

    [IO.File]::WriteAllBytes((Join-Path $root 'debug.pdb'), [byte[]](1))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root -SkipSourceBinding) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a debug-symbol rejection.' }
    Remove-Item -LiteralPath (Join-Path $root 'debug.pdb') -Force
    $caseCount++

    $bad = Get-Content -LiteralPath $manifestPath -Raw
    $bad = $bad -replace '"schema_version": 1', '"schema_version": 1, "schema_version": 1'
    [IO.File]::WriteAllText($manifestPath, $bad, [Text.UTF8Encoding]::new($false))
    $caught = $false
    try { [void](Test-PortablePackageRoot -Root $root -SkipSourceBinding) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a duplicate-property rejection.' }
    [IO.File]::WriteAllText($manifestPath, ((New-SelfTestManifest -Root $root) | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $caseCount++

    [IO.Compression.ZipFile]::CreateFromDirectory($root, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-SelfTestSidecar -Archive $archive
    [void](Test-PortablePackageArchive -Archive $archive -Destination $extract -SkipSourceBinding)
    $caseCount++

    $caught = $false
    try {
      [void](Test-PortablePackageArchive -Archive $archive -Destination ('hibiki-portable-package-relative-' + [guid]::NewGuid().ToString('N')) -SkipSourceBinding)
    } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a relative-extraction-target rejection.' }
    $caseCount++

    $caught = $false
    try {
      [void](Test-PortablePackageArchive -Archive $archive -Destination ($root + '-expected-hash-extract') -ExpectedArchiveSha256 ('0' * 64) -SkipSourceBinding)
    } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected an independently-expected-hash rejection.' }
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
    try { [void](Test-PortablePackageArchive -Archive $unsafeArchive -Destination ($root + '-unsafe-extract') -SkipSourceBinding) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected an unsafe-ZIP-path rejection.' }
    $caseCount++

    $directoryData = [IO.Compression.ZipFile]::Open($directoryDataArchive, [IO.Compression.ZipArchiveMode]::Create)
    try {
      $entry = $directoryData.CreateEntry('hidden/')
      $stream = $entry.Open()
      try { $stream.Write([byte[]](1), 0, 1) } finally { $stream.Dispose() }
    } finally { $directoryData.Dispose() }
    Write-SelfTestSidecar -Archive $directoryDataArchive
    $caught = $false
    try { [void](Test-PortablePackageArchive -Archive $directoryDataArchive -Destination ($root + '-directory-data-extract') -SkipSourceBinding) } catch { $caught = $true }
    if (-not $caught) { throw 'Self-test expected a data-carrying-directory rejection.' }
    $caseCount++

    $reparse = [IO.Compression.ZipFile]::Open($reparseArchive, [IO.Compression.ZipArchiveMode]::Create)
    try {
      $entry = $reparse.CreateEntry('reparse.txt')
      $stream = $entry.Open()
      try { $stream.Write([byte[]](1), 0, 1) } finally { $stream.Dispose() }
      $entry.ExternalAttributes = $entry.ExternalAttributes -bor 0x0400
    } finally { $reparse.Dispose() }
    Write-SelfTestSidecar -Archive $reparseArchive
    $caught = $false
    try {
      [void](Test-PortablePackageArchive -Archive $reparseArchive -Destination ($root + '-reparse-extract') -SkipSourceBinding)
    } catch { $caught = $_.Exception.Message -match 'DOS reparse point' }
    if (-not $caught) { throw 'Self-test expected a DOS-reparse-point rejection.' }
    $caseCount++
    Write-Output "Portable preview package check self-test passed ($caseCount cases)."
  } finally {
    foreach ($path in @($root, $extract, ($root + '-expected-hash-extract'), ($root + '-unsafe-extract'), ($root + '-directory-data-extract'), ($root + '-reparse-extract'))) { Remove-OwnedSelfTestPath -Path $path }
    foreach ($file in @($archive, "$archive.sha256", $unsafeArchive, "$unsafeArchive.sha256", $directoryDataArchive, "$directoryDataArchive.sha256", $reparseArchive, "$reparseArchive.sha256")) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force } }
  }
}

if ($SelfTest) {
  Invoke-PortablePackageSelfTest
  exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Archive') {
  $result = Test-PortablePackageArchive -Archive $ArchivePath -Destination $ExtractTo -SourceRepository $SourceRepository -ExpectedArchiveSha256 $ExpectedArchiveSha256
} else {
  $result = Test-PortablePackageRoot -Root $PackageRoot -SourceRepository $SourceRepository
}
if ($LaunchSmoke) { Invoke-PortablePreviewLaunchSmoke -EntryPoint $result.entry_point }
Write-Output "Portable preview package check passed: source_tag=$($result.source_tag) payload_files=$($result.payload_files) entry_point=$($result.entry_point)"
