#Requires -Version 7
[CmdletBinding(DefaultParameterSetName = 'Package')]
param(
  [Parameter(Mandatory, ParameterSetName = 'Package')]
  [string]$PayloadRoot,
  [Parameter(Mandatory, ParameterSetName = 'Package')]
  [string]$SourceRepository,
  [Parameter(ParameterSetName = 'Package')]
  [ValidatePattern('^v1\.0\.0$')]
  [string]$SourceTag = 'v1.0.0',
  [Parameter(ParameterSetName = 'Package')]
  [string]$OutputDirectory,
  [Parameter(Mandatory, ParameterSetName = 'SelfTest')]
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$script:ManifestName = 'hibiki-portable-preview-manifest-v1.json'
$script:ArchiveName = 'Hibiki-DSP-v1.0.0-portable-win-x64.zip'
$script:EntryPoint = 'Hibiki.DesktopPreview.exe'
$script:OmittedExtensions = @('.pdb')
$script:BlockedExtensions = @(
  '.sys', '.inf', '.cat', '.msi', '.msix', '.appx', '.appxbundle', '.cab', '.vst3',
  '.ps1', '.cmd', '.bat', '.reg', '.cer', '.crt', '.pfx', '.pem', '.key', '.sig'
)

function Assert-NoReparsePoint {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "$Context must not be a reparse point: $Path"
  }
  return $item
}

function Assert-SafeRelativePath {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  if ([string]::IsNullOrWhiteSpace($Path) -or $Path.Length -gt 260 -or
      $Path -match '[\x00-\x1F\x7F-\x9F]' -or $Path -ne $Path.Trim() -or
      [IO.Path]::IsPathRooted($Path) -or $Path.StartsWith('/') -or $Path.EndsWith('/') -or
      $Path.IndexOfAny([char[]]'<>:"\|?*') -ge 0) {
    throw "$Context is not a canonical relative package path: $Path"
  }
  foreach ($segment in @($Path -split '/')) {
    if ([string]::IsNullOrWhiteSpace($segment) -or $segment -in @('.', '..') -or
        $segment.StartsWith('.') -or $segment.EndsWith('.') -or $segment.EndsWith(' ')) {
      throw "$Context has an unsafe path segment: $Path"
    }
    $stem = ($segment -split '\.', 2)[0]
    if ($stem -match '^(?i:con|prn|aux|nul|com[1-9]|lpt[1-9])$') {
      throw "$Context has a Windows reserved path segment: $Path"
    }
  }
}

function Assert-AllowedPayloadPath {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  Assert-SafeRelativePath -Path $Path -Context $Context
  $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
  if ($script:BlockedExtensions -contains $extension) {
    throw "$Context has a prohibited payload extension: $Path"
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
  while ($todo.Count -gt 0) {
    $directory = $todo.Pop()
    [void](Assert-NoReparsePoint -Path $directory -Context "$Context directory")
    foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
      [void](Assert-NoReparsePoint -Path $item.FullName -Context $Context)
      if ($item.PSIsContainer) {
        $todo.Push($item.FullName)
        continue
      }
      $relative = [IO.Path]::GetRelativePath($rootFull, $item.FullName).Replace('\', '/')
      Assert-SafeRelativePath -Path $relative -Context "$Context payload"
      if (-not $seen.Add($relative)) { throw "$Context has a duplicate payload path: $relative" }
      if ($item.Length -lt 1 -or $item.Length -gt 512MB) { throw "$Context payload size is outside bounds: $relative" }
      [void]$records.Add([pscustomobject]@{ FullName = $item.FullName; Relative = $relative; Length = [int64]$item.Length })
    }
  }
  $ordered = [Collections.Generic.List[object]]::new()
  foreach ($record in $records) {
    $index = 0
    while ($index -lt $ordered.Count -and [string]::CompareOrdinal($ordered[$index].Relative, $record.Relative) -lt 0) { $index++ }
    $ordered.Insert($index, $record)
  }
  return @($ordered)
}

function Invoke-GitText {
  param([Parameter(Mandatory)][string]$Repository, [Parameter(Mandatory)][string[]]$Arguments, [Parameter(Mandatory)][string]$FailurePrefix)
  $output = @(& git -C $Repository @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "${FailurePrefix}: $($output -join [Environment]::NewLine)" }
  return ($output -join "`n").Trim()
}

function Assert-PathWithin {
  param([Parameter(Mandatory)][string]$Candidate, [Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Context)
  $candidateFull = [IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not $candidateFull.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Context must remain under ${rootFull}: $candidateFull"
  }
  return $candidateFull
}

function Assert-SafeFutureDirectory {
  param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Context)
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (Test-Path -LiteralPath $full) { throw "$Context already exists: $full" }
  $parent = Split-Path -Parent $full
  while ($parent) {
    if (Test-Path -LiteralPath $parent) { [void](Assert-NoReparsePoint -Path $parent -Context "$Context parent"); break }
    $next = Split-Path -Parent $parent
    if ($next -eq $parent) { break }
    $parent = $next
  }
  return $full
}

function Get-SourceBinding {
  param([Parameter(Mandatory)][string]$Repository, [Parameter(Mandatory)][string]$Tag)
  $sourceRoot = [IO.Path]::GetFullPath($Repository).TrimEnd('\', '/')
  if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) { throw "Source repository does not exist: $sourceRoot" }
  [void](Assert-NoReparsePoint -Path $sourceRoot -Context 'Source repository')
  if ((Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', '--is-inside-work-tree') -FailurePrefix 'Source repository check failed') -ne 'true') {
    throw 'Source repository is not a Git worktree.'
  }
  $status = Invoke-GitText -Repository $sourceRoot -Arguments @('status', '--porcelain') -FailurePrefix 'Source repository status check failed'
  if ($status) { throw 'Source repository must be clean before packaging.' }
  $tagRef = 'refs/tags/' + $Tag
  if ((Invoke-GitText -Repository $sourceRoot -Arguments @('cat-file', '-t', $tagRef) -FailurePrefix 'Source tag lookup failed') -ne 'tag') {
    throw "Source tag must be annotated: $Tag"
  }
  $tagCommit = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', ($tagRef + '^{}')) -FailurePrefix 'Source tag target lookup failed'
  $head = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Source HEAD lookup failed'
  if ($head -cne $tagCommit) { throw "Source repository HEAD must be the detached tag target $tagCommit, got $head" }
  $parentLine = Invoke-GitText -Repository $sourceRoot -Arguments @('rev-list', '--parents', '-n', '1', $tagCommit) -FailurePrefix 'Source parent lookup failed'
  $parents = @($parentLine -split '\s+' | Where-Object { $_ })
  if ($parents.Count -ne 2 -or $parents[0] -cne $tagCommit) { throw 'Source tag target must be a single-parent provenance metadata commit.' }
  $sourceCommit = $parents[1]
  $manifestRelative = 'release/manifests/' + $Tag + '.json'
  $manifestPath = Join-Path $sourceRoot $manifestRelative
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Source tag manifest is missing: $manifestRelative" }
  [void](Assert-NoReparsePoint -Path $manifestPath -Context 'Source tag manifest')
  $provenanceOutput = @(& pwsh -NoProfile -File (Join-Path $sourceRoot 'tools/release-provenance-check.ps1') -Tag $Tag -Repository $sourceRoot 2>&1)
  foreach ($line in $provenanceOutput) { Write-Host ([string]$line) }
  if ($LASTEXITCODE -ne 0) { throw 'Source-tag provenance gate failed.' }
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable -ErrorAction Stop
  $productVersion = $Tag.Substring(1)
  if ($manifest.source_tag -cne $Tag -or $manifest.product_version -cne $productVersion -or
      $manifest.source_commit -cne $sourceCommit -or [string]::IsNullOrWhiteSpace([string]$manifest.distribution_id)) {
    throw 'Source tag manifest does not match the package source binding.'
  }
  $changedText = Invoke-GitText -Repository $sourceRoot -Arguments @('diff', '--name-only', $sourceCommit, $tagCommit) -FailurePrefix 'Source tag diff check failed'
  $changed = @($changedText -split "`r?`n" | Where-Object { $_ })
  if ($changed.Count -ne 1 -or $changed[0] -cne $manifestRelative) {
    throw 'Source tag provenance metadata commit contains unexpected files.'
  }
  return [pscustomobject]@{
    SourceRoot = $sourceRoot
    SourceTag = $Tag
    ProductVersion = $productVersion
    TagCommit = $tagCommit
    SourceCommit = $sourceCommit
    SourceManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    DistributionId = [string]$manifest.distribution_id
  }
}

function Get-PackagerCommit {
  $status = Invoke-GitText -Repository $repo -Arguments @('status', '--porcelain', '--untracked-files=no') -FailurePrefix 'Packager repository status check failed'
  if ($status) { throw 'Packager repository must have no tracked modifications.' }
  return Invoke-GitText -Repository $repo -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Packager commit lookup failed'
}

function Copy-DesktopPayload {
  param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Stage)
  $copied = 0
  $omitted = 0
  foreach ($record in @(Get-SafeRegularFiles -Root $Source -Context 'DesktopCompat payload')) {
    $extension = [IO.Path]::GetExtension($record.Relative).ToLowerInvariant()
    if ($script:OmittedExtensions -contains $extension) { $omitted++; continue }
    Assert-AllowedPayloadPath -Path $record.Relative -Context 'DesktopCompat payload'
    if ([string]::Equals($record.Relative, $script:ManifestName, [StringComparison]::OrdinalIgnoreCase)) {
      throw "DesktopCompat payload must not provide the package manifest name: $($record.Relative)"
    }
    $destination = Join-Path $Stage ($record.Relative.Replace('/', [IO.Path]::DirectorySeparatorChar))
    $parent = Split-Path -Parent $destination
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    [IO.File]::Copy($record.FullName, $destination, $false)
    $copied++
  }
  if ($copied -lt 1) { throw 'DesktopCompat payload contains no distributable files.' }
  $entryPoint = Join-Path $Stage $script:EntryPoint
  if (-not (Test-Path -LiteralPath $entryPoint -PathType Leaf)) { throw "DesktopCompat payload is missing $script:EntryPoint" }
  [void](Assert-NoReparsePoint -Path $entryPoint -Context 'DesktopCompat entry point')
  return [pscustomobject]@{ Copied = $copied; OmittedDebugSymbols = $omitted }
}

function Add-PackageTextFiles {
  param([Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$SourceRoot)
  $notices = Join-Path $SourceRoot 'THIRD_PARTY.yml'
  [void](Assert-NoReparsePoint -Path $notices -Context 'Third-party notices')
  [IO.File]::Copy($notices, (Join-Path $Stage 'THIRD_PARTY.yml'), $false)
  $readme = @'
Hibiki DSP v1.0.0 Portable User-Space Preview

Start only after the SHA-256 of the ZIP matches the official .sha256 sidecar.
Unzip the archive, then start Hibiki.DesktopPreview.exe from its root.

This is an unsigned Windows x64 DesktopCompat control-surface preview. It is
self-contained and includes its attributable .NET runtime payload; the included
THIRD_PARTY.yml names the source-tag dependency notices.

It does not install or load a driver, service, endpoint, installer, updater, or
Engine Preview. It does not control system audio settings or produce physical
audio. The app may save its own UI preferences under %LOCALAPPDATA%\Hibiki DSP.

Windows may show an unsigned/reputation warning. Continue only when you obtained
this archive from the official Hibiki DSP v1.0.0 Release and its SHA-256 matches.
'@
  [IO.File]::WriteAllText((Join-Path $Stage 'PORTABLE_PREVIEW_README.txt'), $readme, [Text.UTF8Encoding]::new($false))
}

function Get-StageRecords {
  param([Parameter(Mandatory)][string]$Stage)
  $records = [Collections.Generic.List[object]]::new()
  foreach ($record in @(Get-SafeRegularFiles -Root $Stage -Context 'Package staging')) {
    if ([string]::Equals($record.Relative, $script:ManifestName, [StringComparison]::OrdinalIgnoreCase)) { continue }
    Assert-AllowedPayloadPath -Path $record.Relative -Context 'Package staging'
    [void]$records.Add([ordered]@{
      path = $record.Relative
      sha256 = (Get-FileHash -LiteralPath $record.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
      size = [int64]$record.Length
    })
  }
  return @($records)
}

function Write-PortableManifest {
  param([Parameter(Mandatory)]$Binding, [Parameter(Mandatory)][string]$Stage, [Parameter(Mandatory)][string]$PackagerCommit)
  $manifest = [ordered]@{
    schema_version = 1
    package_kind = 'portable-user-space-preview'
    product_version = $Binding.ProductVersion
    source_tag = $Binding.SourceTag
    tag_commit = $Binding.TagCommit
    source_commit = $Binding.SourceCommit
    source_manifest_sha256 = $Binding.SourceManifestSha256
    packager_commit = $PackagerCommit
    distribution_id = $Binding.DistributionId
    platform = [ordered]@{ target = 'windows-11'; os_min_build = 26100; architecture = 'x64' }
    entry_point = $script:EntryPoint
    runtime = 'self-contained-dotnet-win-x64'
    driver = 'not-included'
    installer = 'not-included'
    limitations = @(
      'Unsigned Windows user-space preview; verify the release SHA-256 before launch.',
      'No driver, service, endpoint, Engine Preview, WaveRT streaming, or physical audio.',
      'Per-user DesktopCompat UI preferences may be stored under %LOCALAPPDATA%\\Hibiki DSP.'
    )
    files = @(Get-StageRecords -Stage $Stage)
  }
  $manifestPath = Join-Path $Stage $script:ManifestName
  [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Remove-OwnedTemporaryDirectory {
  param([Parameter(Mandatory)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return }
  $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (-not $full.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
      -not ([IO.Path]::GetFileName($full).StartsWith('hibiki-portable-preview-', [StringComparison]::Ordinal))) {
    throw "Refusing to remove unexpected temporary directory: $full"
  }
  [void](Assert-NoReparsePoint -Path $full -Context 'Temporary package directory')
  Remove-Item -LiteralPath $full -Recurse -Force
}

function Invoke-PortablePreviewPackageSelfTest {
  Assert-SafeRelativePath -Path 'runtime/System.Private.CoreLib.dll' -Context 'self-test valid path'
  $cases = @('..\escape', 'CON.txt', 'folder/trailing. ', 'folder/../escape', 'folder/file.ps1')
  foreach ($case in $cases) {
    $caught = $false
    try { Assert-AllowedPayloadPath -Path $case -Context 'self-test unsafe path' } catch { $caught = $true }
    if (-not $caught) { throw "Portable package self-test expected rejection: $case" }
  }
  Write-Output 'Portable preview package self-test passed (6 cases).'
}

if ($SelfTest) {
  Invoke-PortablePreviewPackageSelfTest
  exit 0
}

$binding = Get-SourceBinding -Repository $SourceRepository -Tag $SourceTag
$expectedPayloadRoot = [IO.Path]::GetFullPath((Join-Path $binding.SourceRoot '.local/preview/DesktopCompat')).TrimEnd('\', '/')
$payloadFull = [IO.Path]::GetFullPath($PayloadRoot).TrimEnd('\', '/')
if ($payloadFull -cne $expectedPayloadRoot) {
  throw "PayloadRoot must be the source-tag DesktopCompat output: $expectedPayloadRoot"
}
[void](Assert-NoReparsePoint -Path $payloadFull -Context 'DesktopCompat payload root')
$packagerCommit = Get-PackagerCommit

$localRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local')).TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $localRoot ('portable-preview-' + $SourceTag + '-' + [guid]::NewGuid().ToString('N'))
}
$outputFull = Assert-PathWithin -Candidate $OutputDirectory -Root $localRoot -Context 'Package output directory'
$outputFull = Assert-SafeFutureDirectory -Path $outputFull -Context 'Package output directory'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-portable-preview-stage-' + [guid]::NewGuid().ToString('N'))
$extract = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-portable-preview-extract-' + [guid]::NewGuid().ToString('N'))

try {
  [IO.Directory]::CreateDirectory($outputFull) | Out-Null
  [IO.Directory]::CreateDirectory($stage) | Out-Null
  $copy = Copy-DesktopPayload -Source $payloadFull -Stage $stage
  Add-PackageTextFiles -Stage $stage -SourceRoot $binding.SourceRoot
  Write-PortableManifest -Binding $binding -Stage $stage -PackagerCommit $packagerCommit

  $checker = Join-Path $repo 'tools/portable-preview-package-check.ps1'
  & pwsh -NoProfile -File $checker -PackageRoot $stage
  if ($LASTEXITCODE -ne 0) { throw 'Portable package root validation failed.' }

  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = Join-Path $outputFull $script:ArchiveName
  [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
  $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$archive.sha256", "$hash *$script:ArchiveName", [Text.UTF8Encoding]::new($false))

  & pwsh -NoProfile -File $checker -ArchivePath $archive -ExtractTo $extract -LaunchSmoke
  if ($LASTEXITCODE -ne 0) { throw 'Portable archive validation or launch smoke failed.' }
  Write-Output "Portable preview package created: $archive"
  Write-Output "Portable preview SHA-256 sidecar: $archive.sha256"
  Write-Output "Portable preview package summary: payload_files=$($copy.Copied) omitted_debug_symbols=$($copy.OmittedDebugSymbols) source_tag=$($binding.SourceTag) tag_commit=$($binding.TagCommit) source_commit=$($binding.SourceCommit) packager_commit=$packagerCommit"
} finally {
  Remove-OwnedTemporaryDirectory -Path $stage
  Remove-OwnedTemporaryDirectory -Path $extract
}
