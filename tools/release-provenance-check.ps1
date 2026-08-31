#Requires -Version 7
[CmdletBinding()]
param(
  [string]$Tag,
  [string]$Repository,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:SourceTagPattern = '^v[0-9]+\.[0-9]+\.[0-9]+[A-Za-z0-9._:+-]{0,32}$'
$script:ManifestRoot = 'release/manifests'
$script:SchemaPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'schemas/source-release-manifest-v1.schema.json'
$script:SchemaDependenciesRegistered = $false

function Invoke-GitText {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string[]]$Arguments,
    [Parameter(Mandatory)][string]$FailurePrefix
  )

  $output = @(& git -C $RepositoryPath @Arguments 2>&1)
  $exitCode = $LASTEXITCODE
  if ($exitCode -ne 0) {
    $detail = (@($output | ForEach-Object { $_.ToString().Trim() } | Where-Object { $_ -ne '' }) -join "`n")
    if ([string]::IsNullOrWhiteSpace($detail)) { $detail = 'no diagnostic output' }
    throw ($FailurePrefix + ': ' + $detail)
  }
  return @($output | ForEach-Object { $_.ToString() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-GitSingleLine {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string[]]$Arguments,
    [Parameter(Mandatory)][string]$FailurePrefix
  )

  $lines = @(Invoke-GitText -RepositoryPath $RepositoryPath -Arguments $Arguments -FailurePrefix $FailurePrefix)
  if ($lines.Count -ne 1) {
    throw ($FailurePrefix + ' must return exactly one line.')
  }
  return $lines[0].Trim()
}

function Assert-SourceTagName {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$TagName
  )

  if ($TagName -notmatch $script:SourceTagPattern) {
    throw "Release tag '$TagName' must match the SourceReleaseManifest source_tag format."
  }
  $null = Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('check-ref-format', '--allow-onelevel', ('refs/tags/' + $TagName)) `
    -FailurePrefix "Release tag '$TagName' is not a valid Git tag ref"
}

function Get-ManifestString {
  param(
    [Parameter(Mandatory)][object]$Manifest,
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$ManifestPath
  )

  if ($Manifest -isnot [pscustomobject] -or
      -not ($Manifest.PSObject.Properties.Name -contains $Name)) {
    throw "Release manifest '$ManifestPath' is missing required $Name."
  }
  $value = $Manifest.$Name
  if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
    throw "Release manifest '$ManifestPath' $Name must be a non-empty string."
  }
  return $value
}

function Assert-ReleaseManifestSchema {
  param(
    [Parameter(Mandatory)][string]$Json,
    [Parameter(Mandatory)][string]$ManifestPath,
    [Parameter(Mandatory)][string]$SchemaFile
  )

  if (-not (Test-Path -LiteralPath $SchemaFile -PathType Leaf)) {
    throw "SourceReleaseManifest v1 schema is missing: $SchemaFile"
  }
  try {
    if (-not $script:SchemaDependenciesRegistered) {
      $printableSchema = Join-Path (Split-Path -Parent $SchemaFile) 'printable-string-v1.schema.json'
      if (-not (Test-Path -LiteralPath $printableSchema -PathType Leaf)) {
        throw "SourceReleaseManifest printable-string schema is missing: $printableSchema"
      }
      $null = Test-Json -Json '{}' -SchemaFile $printableSchema -ErrorAction Stop
      $printable = [Json.Schema.JsonSchema]::FromFile($printableSchema)
      [Json.Schema.SchemaRegistry]::Global.Register($printable)
      $script:SchemaDependenciesRegistered = $true
    }

    $schemaErrors = @()
    $validation = @(Test-Json -Json $Json -SchemaFile $SchemaFile `
      -ErrorAction SilentlyContinue -ErrorVariable +schemaErrors)
  } catch {
    throw "Release manifest '$ManifestPath' schema validation could not run: $($_.Exception.Message)"
  }

  if ($validation.Count -ne 1 -or $validation[0] -isnot [bool]) {
    throw "Release manifest '$ManifestPath' schema validation returned an invalid result."
  }
  if (-not [bool]$validation[0]) {
    throw "Release manifest '$ManifestPath' must satisfy SourceReleaseManifest v1 schema."
  }
}

function Get-DirectAnnotatedTagCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$TagName,
    [Parameter(Mandatory)][string]$TagRef
  )

  $tagLines = @(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-p', $TagRef) `
    -FailurePrefix "Release tag '$TagName' cannot read its annotated tag object")
  if ($tagLines.Count -lt 4) {
    throw "Release tag '$TagName' must contain canonical annotated tag headers."
  }
  $objectHeader = $tagLines[0].Trim()
  $typeHeader = $tagLines[1].Trim()
  $tagHeader = $tagLines[2].Trim()
  $taggerHeader = $tagLines[3].Trim()
  if ($objectHeader -notmatch '^object [0-9a-f]{40}$' -or
      $tagHeader -notmatch '^tag .+$' -or
      $taggerHeader -notmatch '^tagger .+$') {
    throw "Release tag '$TagName' must contain canonical annotated tag headers."
  }
  if ($typeHeader -cne 'type commit') {
    throw "Release tag '$TagName' must directly target a commit; nested tags are rejected."
  }

  $tagCommit = $objectHeader.Substring('object '.Length)
  $targetObjectType = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-t', $tagCommit) `
    -FailurePrefix "Release tag '$TagName' direct target cannot be read"
  if ($targetObjectType -cne 'commit') {
    throw "Release tag '$TagName' must directly target a commit."
  }
  return $tagCommit
}

function Assert-SafeSourceBlobPath {
  param(
    [Parameter(Mandatory)][string]$RelativePath
  )

  if ([string]::IsNullOrWhiteSpace($RelativePath) -or
      [IO.Path]::IsPathRooted($RelativePath) -or
      $RelativePath.StartsWith('/') -or
      $RelativePath.Contains([char]92) -or
      $RelativePath.Contains('//') -or
      $RelativePath -match '(^|/)(?:\.|\.\.)(?:/|$)' -or
      $RelativePath -match '[\\:*?"<>|\[\]]' -or
      $RelativePath -match '[\x00-\x1F\x7F]') {
    throw "Declared source path '$RelativePath' is not safe."
  }
}

function Get-BlobObjectIdFromCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$CommitSha,
    [Parameter(Mandatory)][string]$RelativePath
  )

  Assert-SafeSourceBlobPath -RelativePath $RelativePath

  $lsOutput = @(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('ls-tree', $CommitSha, '--', $RelativePath) `
    -FailurePrefix "Cannot find path '$RelativePath' in commit $CommitSha")
  if ($lsOutput.Count -eq 0) {
    throw "Declared path '$RelativePath' does not exist in commit $CommitSha."
  }
  $regularPattern = '^100644 blob ([0-9a-f]{40})' + [char]9 + [regex]::Escape($RelativePath) + '$'
  $entryMatch = if ($lsOutput.Count -eq 1) { [regex]::Match($lsOutput[0], $regularPattern) } else { $null }
  if ($null -eq $entryMatch -or -not $entryMatch.Success) {
    throw "Declared path '$RelativePath' in commit $CommitSha must be a regular 100644 blob."
  }
  return $entryMatch.Groups[1].Value
}

function Get-BlobBytesFromCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$CommitSha,
    [Parameter(Mandatory)][string]$RelativePath,
    [ValidateRange(1, 67108864)][Int64]$MaxBytes = 67108864
  )

  $blobSha = Get-BlobObjectIdFromCommit -RepositoryPath $RepositoryPath -CommitSha $CommitSha -RelativePath $RelativePath
  $blobSizeText = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-s', $blobSha) `
    -FailurePrefix "Cannot determine size for declared path '$RelativePath' in $CommitSha"
  if ($blobSizeText -notmatch '^[0-9]+$') {
    throw "Declared path '$RelativePath' in $CommitSha returned an invalid blob size."
  }
  $blobSize = [Int64]$blobSizeText
  if ($blobSize -gt $MaxBytes) {
    throw "Declared path '$RelativePath' in $CommitSha exceeds the $MaxBytes-byte provenance read limit."
  }
  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = 'git'
  $null = $psi.ArgumentList.Add('-C')
  $null = $psi.ArgumentList.Add($RepositoryPath)
  $null = $psi.ArgumentList.Add('cat-file')
  $null = $psi.ArgumentList.Add('blob')
  $null = $psi.ArgumentList.Add($blobSha)
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $proc = $null
  $ms = [System.IO.MemoryStream]::new()
  try {
    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($null -eq $proc) {
      throw "git cat-file blob could not start for '$RelativePath' in $CommitSha"
    }
    $proc.StandardOutput.BaseStream.CopyTo($ms)
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) {
      $detail = $stderr.Trim()
      if ([string]::IsNullOrWhiteSpace($detail)) { $detail = 'no diagnostic output' }
      throw "git cat-file blob failed for '$RelativePath' in ${CommitSha}: $detail"
    }
    return ,$ms.ToArray()
  } finally {
    $ms.Dispose()
    if ($null -ne $proc) { $proc.Dispose() }
  }
}

function Get-BlobSha256FromCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$CommitSha,
    [Parameter(Mandatory)][string]$RelativePath
  )

  $rawBytes = Get-BlobBytesFromCommit -RepositoryPath $RepositoryPath -CommitSha $CommitSha -RelativePath $RelativePath
  $shaObj = [System.Security.Cryptography.SHA256]::Create()
  try {
    $hashBytes = $shaObj.ComputeHash($rawBytes)
    return ([System.BitConverter]::ToString($hashBytes) -replace '-', '').ToLowerInvariant()
  } finally {
    $shaObj.Dispose()
  }
}

function Get-TextBlobFromCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$CommitSha,
    [Parameter(Mandatory)][string]$RelativePath
  )

  $rawBytes = Get-BlobBytesFromCommit -RepositoryPath $RepositoryPath -CommitSha $CommitSha -RelativePath $RelativePath -MaxBytes 4MB
  try {
    return [System.Text.UTF8Encoding]::new($false, $true).GetString($rawBytes)
  } catch {
    throw "Declared text path '$RelativePath' in $CommitSha must be valid UTF-8 text: $($_.Exception.Message)"
  }
}

function Test-ReleaseTagProvenance {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$TagName,
    [string]$SchemaFile = $script:SchemaPath
  )

  $insideWorkTree = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('rev-parse', '--is-inside-work-tree') `
    -FailurePrefix "Release provenance repository '$RepositoryPath' is unavailable"
  if ($insideWorkTree -cne 'true') {
    throw "Release provenance repository '$RepositoryPath' is not a Git worktree."
  }

  Assert-SourceTagName -RepositoryPath $RepositoryPath -TagName $TagName
  $tagRef = 'refs/tags/' + $TagName
  $tagObjectType = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-t', $tagRef) `
    -FailurePrefix "Release tag '$TagName' cannot be read"
  if ($tagObjectType -cne 'tag') {
    throw "Release tag '$TagName' must be annotated; lightweight tags are rejected."
  }

  $tagCommit = Get-DirectAnnotatedTagCommit -RepositoryPath $RepositoryPath -TagName $TagName -TagRef $tagRef

  $parentLine = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('rev-list', '--parents', '-n', '1', $tagCommit) `
    -FailurePrefix "Release tag '$TagName' cannot resolve its target parents"
  $parents = @($parentLine -split '\s+' | Where-Object { $_ -ne '' })
  if ($parents.Count -ne 2 -or $parents[0] -cne $tagCommit -or $parents[1] -notmatch '^[0-9a-f]{40}$') {
    throw "Release tag '$TagName' must target a single-parent provenance metadata commit."
  }
  $sourceCommit = $parents[1]

  $manifestPath = $script:ManifestRoot + '/' + $TagName + '.json'
  $treeEntries = @(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('ls-tree', $tagCommit, '--', $manifestPath) `
    -FailurePrefix "Release tag '$TagName' cannot inspect manifest '$manifestPath'")
  if ($treeEntries.Count -eq 0) {
    throw "Release tag '$TagName' is missing text manifest '$manifestPath'."
  }
  $regularBlobPattern = '^100644 blob [0-9a-f]{40}' + [char]9 + [regex]::Escape($manifestPath) + '$'
  if ($treeEntries.Count -ne 1 -or $treeEntries[0] -notmatch $regularBlobPattern) {
    throw "Release manifest '$manifestPath' must be a regular 100644 blob."
  }
  $manifestObject = $tagCommit + ':' + $manifestPath
  $manifestText = (@(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('show', $manifestObject) `
    -FailurePrefix "Release tag '$TagName' is missing text manifest '$manifestPath'") -join "`n")
  try {
    $manifest = $manifestText | ConvertFrom-Json
  } catch {
    throw "Release manifest '$manifestPath' must contain valid JSON: $($_.Exception.Message)"
  }
  Assert-ReleaseManifestSchema -Json $manifestText -ManifestPath $manifestPath -SchemaFile $SchemaFile

  $releaseKind = Get-ManifestString -Manifest $manifest -Name 'release_kind' -ManifestPath $manifestPath
  if ($releaseKind -cne 'source-only') {
    throw "Release manifest '$manifestPath' release_kind must be source-only."
  }
  if ($manifest.artifacts -isnot [pscustomobject]) {
    throw "Release manifest '$manifestPath' artifacts must be an object."
  }
  foreach ($artifactName in @('driver', 'installer')) {
    if (-not ($manifest.artifacts.PSObject.Properties.Name -contains $artifactName) -or
        $manifest.artifacts.$artifactName -isnot [string] -or
        $manifest.artifacts.$artifactName -cne 'not-published') {
      throw "Release manifest '$manifestPath' artifact status '$artifactName' must be not-published."
    }
  }

  $productVersion = Get-ManifestString -Manifest $manifest -Name 'product_version' -ManifestPath $manifestPath
  $expectedProductVersion = $TagName.Substring(1)
  if ($productVersion -cne $expectedProductVersion) {
    throw "Release manifest '$manifestPath' product_version '$productVersion' does not match tag version '$expectedProductVersion'."
  }
  $manifestTag = Get-ManifestString -Manifest $manifest -Name 'source_tag' -ManifestPath $manifestPath
  if ($manifestTag -cne $TagName) {
    throw "Release manifest '$manifestPath' source_tag '$manifestTag' does not match tag '$TagName'."
  }
  $manifestCommit = Get-ManifestString -Manifest $manifest -Name 'source_commit' -ManifestPath $manifestPath
  if ($manifestCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Release manifest '$manifestPath' source_commit must be a 40-character lowercase commit string."
  }
  if ($manifestCommit -cne $sourceCommit) {
    throw "Release manifest '$manifestPath' source_commit does not match the tag metadata commit parent."
  }

  $changedPaths = @(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('diff', '--name-status', '--no-renames', $sourceCommit, $tagCommit) `
    -FailurePrefix "Release tag '$TagName' metadata diff cannot be read")
  $expectedChange = '^A' + [char]9 + [regex]::Escape($manifestPath) + '$'
  if ($changedPaths.Count -ne 1 -or $changedPaths[0] -notmatch $expectedChange) {
    throw "Release tag '$TagName' provenance metadata commit may change only '$manifestPath'."
  }

  $expectedSourcePaths = [ordered]@{
    toolchain_lock = 'build/toolchain-lock.yml'
    dependency_lock = 'THIRD_PARTY.yml'
    sbom = ('release/provenance/' + $TagName + '/SBOM.spdx.json')
    release_notes = ('release/provenance/' + $TagName + '/RELEASE_NOTES.md')
    notices = ('release/provenance/' + $TagName + '/NOTICE.md')
  }
  foreach ($fieldName in $expectedSourcePaths.Keys) {
    $item = $manifest.PSObject.Properties[$fieldName].Value
    if ($item.path -cne $expectedSourcePaths[$fieldName]) {
      throw "Release manifest '$manifestPath' $fieldName.path must be '$($expectedSourcePaths[$fieldName])'."
    }
  }

  $profileText = Get-TextBlobFromCommit -RepositoryPath $RepositoryPath -CommitSha $sourceCommit -RelativePath 'config/distribution-profile.yml'
  $profileMatches = [regex]::Matches($profileText, '(?m)^distribution_id:\s*(?<id>[^#\s]+)\s*(?:#.*)?$')
  if ($profileMatches.Count -ne 1) {
    throw "Source commit $sourceCommit must contain exactly one top-level distribution_id in config/distribution-profile.yml."
  }
  $manifestDistributionId = Get-ManifestString -Manifest $manifest -Name 'distribution_id' -ManifestPath $manifestPath
  $profileDistributionId = $profileMatches[0].Groups['id'].Value
  if ($manifestDistributionId -cne $profileDistributionId) {
    throw "Release manifest '$manifestPath' distribution_id '$manifestDistributionId' does not match config/distribution-profile.yml '$profileDistributionId'."
  }

  $sbomText = Get-TextBlobFromCommit -RepositoryPath $RepositoryPath -CommitSha $sourceCommit -RelativePath $manifest.sbom.path
  try {
    $sbom = $sbomText | ConvertFrom-Json -AsHashtable
  } catch {
    throw "Declared SBOM '$($manifest.sbom.path)' must contain valid SPDX JSON: $($_.Exception.Message)"
  }
  if ($sbom -isnot [System.Collections.IDictionary]) {
    throw "Declared SBOM '$($manifest.sbom.path)' must contain an SPDX JSON object."
  }
  foreach ($spdxField in @('spdxVersion', 'SPDXID', 'name')) {
    if (-not $sbom.ContainsKey($spdxField) -or
        $sbom[$spdxField] -isnot [string] -or
        [string]::IsNullOrWhiteSpace($sbom[$spdxField])) {
      throw "Declared SBOM '$($manifest.sbom.path)' must contain non-empty $spdxField."
    }
  }
  if ($sbom['spdxVersion'] -notmatch '^SPDX-[0-9]+\.[0-9]+$') {
    throw "Declared SBOM '$($manifest.sbom.path)' spdxVersion must use an SPDX version identifier."
  }
  foreach ($textFieldName in @('release_notes', 'notices')) {
    $textPath = $manifest.PSObject.Properties[$textFieldName].Value.path
    $textContent = Get-TextBlobFromCommit -RepositoryPath $RepositoryPath -CommitSha $sourceCommit -RelativePath $textPath
    if ([string]::IsNullOrWhiteSpace($textContent)) {
      throw "Declared text provenance '$textPath' must not be empty."
    }
  }

  # Verify all declared source blobs exist in sourceCommit and match SHA-256
  $declaredItems = @(
    $manifest.toolchain_lock,
    $manifest.dependency_lock,
    $manifest.sbom,
    $manifest.release_notes,
    $manifest.notices
  )
  if ($manifest.PSObject.Properties.Name -contains 'source_files' -and $null -ne $manifest.source_files) {
    $declaredItems += @($manifest.source_files)
  }

  $seenPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
  foreach ($item in $declaredItems) {
    $p = $item.path
    $declaredSha = $item.sha256.ToLowerInvariant()
    if (-not $seenPaths.Add($p)) {
      throw "Release manifest '$manifestPath' contains duplicate declared path '$p'."
    }
    $actualSha = Get-BlobSha256FromCommit -RepositoryPath $RepositoryPath -CommitSha $sourceCommit -RelativePath $p
    if ($actualSha -cne $declaredSha) {
      throw "Declared path '$p' hash mismatch in $sourceCommit (expected $declaredSha, computed $actualSha)."
    }
  }

  return [pscustomobject]@{
    tag = $TagName
    tag_commit = $tagCommit
    source_commit = $sourceCommit
    manifest_path = $manifestPath
  }
}

function New-ReleaseProvenanceFixture {
  param(
    [Parameter(Mandatory)][string]$TagName,
    [switch]$LightweightTag,
    [switch]$MissingManifest,
    [switch]$MalformedManifest,
    [object]$ManifestTag,
    [object]$ManifestCommit,
    [switch]$AddNonProvenancePath,
    [switch]$MultipleParents,
    [switch]$ManifestSymlink,
    [switch]$NestedAnnotatedTag,
    [switch]$ZeroParents,
    [switch]$HeaderLikeTagMessage,
    [switch]$CorruptedFileHash,
    [switch]$DuplicateFilePath,
    [switch]$DuplicateRequiredFilePath,
    [switch]$MissingSourceFile,
    [switch]$UnsafeSourceFilePath,
    [switch]$SourceFileSymlink,
    [switch]$WrongRolePath,
    [switch]$WrongProductVersion,
    [switch]$WrongDistributionId,
    [switch]$PublishedArtifact,
    [switch]$MalformedSbom,
    [switch]$EmptyReleaseNotes,
    [switch]$EmptySourceFiles,
    [switch]$EmptyTests
  )

  $root = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-release-provenance-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $root -Force | Out-Null
  try {
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('init', '--initial-branch', 'main') -FailurePrefix 'Self-test git init failed'
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('config', 'user.name', 'Hibiki self-test') -FailurePrefix 'Self-test git user.name setup failed'
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('config', 'user.email', 'hibiki-self-test@example.invalid') -FailurePrefix 'Self-test git user.email setup failed'

    $provenanceDirectory = Join-Path $root ('release/provenance/' + $TagName)
    foreach ($directory in @(
      (Join-Path $root 'source'),
      (Join-Path $root 'build'),
      (Join-Path $root 'config'),
      $provenanceDirectory
    )) {
      New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $root 'source/payload.txt') -Value 'source-only fixture' -NoNewline -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'build/toolchain-lock.yml') -Value 'toolchain-lock' -NoNewline -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'THIRD_PARTY.yml') -Value 'deps-lock' -NoNewline -Encoding utf8
    Set-Content -LiteralPath (Join-Path $root 'config/distribution-profile.yml') -Value "schema_version: 1`ndistribution_id: hibiki-self-test" -NoNewline -Encoding utf8
    $sbomPath = Join-Path $provenanceDirectory 'SBOM.spdx.json'
    $sbomText = if ($MalformedSbom) {
      '{"spdxVersion":"SPDX-2.3"}'
    } else {
      '{"spdxVersion":"SPDX-2.3","SPDXID":"SPDXRef-DOCUMENT","name":"Hibiki self-test source"}'
    }
    Set-Content -LiteralPath $sbomPath -Value $sbomText -NoNewline -Encoding utf8
    $releaseNotesText = if ($EmptyReleaseNotes) { '' } else { '# Release Notes' }
    Set-Content -LiteralPath (Join-Path $provenanceDirectory 'RELEASE_NOTES.md') -Value $releaseNotesText -NoNewline -Encoding utf8
    Set-Content -LiteralPath (Join-Path $provenanceDirectory 'NOTICE.md') -Value 'Third party notices' -NoNewline -Encoding utf8

    $null = Invoke-GitText -RepositoryPath $root -Arguments @('add', '--all') -FailurePrefix 'Self-test source add failed'
    if ($SourceFileSymlink) {
      $symlinkPayloadPath = Join-Path $root '.source-symlink-payload'
      Set-Content -LiteralPath $symlinkPayloadPath -Value 'source/payload-target.txt' -NoNewline -Encoding utf8
      $symlinkBlob = Get-GitSingleLine -RepositoryPath $root -Arguments @('hash-object', '-w', '--', $symlinkPayloadPath) -FailurePrefix 'Self-test source symlink blob failed'
      Remove-Item -LiteralPath $symlinkPayloadPath -Force
      $sourceSymlinkIndexEntry = '120000,' + $symlinkBlob + ',source/payload.txt'
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('update-index', '--cacheinfo', $sourceSymlinkIndexEntry) -FailurePrefix 'Self-test source symlink index update failed'
    }
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('commit', '-m', 'source input') -FailurePrefix 'Self-test source commit failed'
    $sourceCommit = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Self-test source commit lookup failed'

    $shaOf = {
      param($p)
      $rawBytes = [System.IO.File]::ReadAllBytes((Join-Path $root $p))
      $hash = [System.Security.Cryptography.SHA256]::HashData($rawBytes)
      return ([System.BitConverter]::ToString($hash) -replace '-', '').ToLowerInvariant()
    }

    $manifestIndexEntry = $null
    if (-not $MissingManifest) {
      $manifestPath = Join-Path $root ('release/manifests/' + $TagName + '.json')
      New-Item -ItemType Directory -Path (Split-Path -Parent $manifestPath) -Force | Out-Null
      if ($MalformedManifest) {
        $manifestValue = [ordered]@{
          source_tag = if (-not $PSBoundParameters.ContainsKey('ManifestTag')) { $TagName } else { $ManifestTag }
          source_commit = if (-not $PSBoundParameters.ContainsKey('ManifestCommit')) { $sourceCommit } else { $ManifestCommit }
        }
      } else {
        $payloadHash = if ($CorruptedFileHash) { '0' * 64 } else { & $shaOf 'source/payload.txt' }
        $sourceFilePath = if ($UnsafeSourceFilePath) {
          'source\\payload.txt'
        } elseif ($MissingSourceFile) {
          'source/missing.txt'
        } else {
          'source/payload.txt'
        }
        $sourceFiles = @()
        if (-not $EmptySourceFiles) {
          $sourceFiles += [ordered]@{ path = $sourceFilePath; sha256 = $payloadHash }
        }
        if ($DuplicateFilePath) {
          $sourceFiles += [ordered]@{ path = 'source/payload.txt'; sha256 = $payloadHash }
        }
        if ($DuplicateRequiredFilePath) {
          $sourceFiles += [ordered]@{ path = 'build/toolchain-lock.yml'; sha256 = & $shaOf 'build/toolchain-lock.yml' }
        }
        $testLabels = @()
        if (-not $EmptyTests) {
          $testLabels += 'self-test'
        }
        $manifestValue = [ordered]@{
          schema_version = 1
          release_kind = 'source-only'
          product_version = if ($WrongProductVersion) { '9.9.9' } else { $TagName.Substring(1) }
          source_tag = if (-not $PSBoundParameters.ContainsKey('ManifestTag')) { $TagName } else { $ManifestTag }
          source_commit = if (-not $PSBoundParameters.ContainsKey('ManifestCommit')) { $sourceCommit } else { $ManifestCommit }
          distribution_id = if ($WrongDistributionId) { 'wrong-distribution' } else { 'hibiki-self-test' }
          toolchain_lock = [ordered]@{ path = if ($WrongRolePath) { 'source/toolchain.lock' } else { 'build/toolchain-lock.yml' }; sha256 = & $shaOf 'build/toolchain-lock.yml' }
          dependency_lock = [ordered]@{ path = 'THIRD_PARTY.yml'; sha256 = & $shaOf 'THIRD_PARTY.yml' }
          sbom = [ordered]@{ path = ('release/provenance/' + $TagName + '/SBOM.spdx.json'); sha256 = & $shaOf ('release/provenance/' + $TagName + '/SBOM.spdx.json') }
          release_notes = [ordered]@{ path = ('release/provenance/' + $TagName + '/RELEASE_NOTES.md'); sha256 = & $shaOf ('release/provenance/' + $TagName + '/RELEASE_NOTES.md') }
          notices = [ordered]@{ path = ('release/provenance/' + $TagName + '/NOTICE.md'); sha256 = & $shaOf ('release/provenance/' + $TagName + '/NOTICE.md') }
          source_files = $sourceFiles
          artifacts = [ordered]@{
            driver = if ($PublishedArtifact) { 'published' } else { 'not-published' }
            installer = 'not-published'
          }
          tests = $testLabels
        }
      }
      $manifestText = $manifestValue | ConvertTo-Json -Depth 10
      if ($ManifestSymlink) {
        $symlinkPayloadPath = Join-Path $root '.manifest-symlink-payload.json'
        $manifestText | Set-Content -LiteralPath $symlinkPayloadPath -Encoding utf8
        $symlinkBlob = Get-GitSingleLine -RepositoryPath $root -Arguments @('hash-object', '-w', '--', $symlinkPayloadPath) -FailurePrefix 'Self-test symlink manifest blob failed'
        Remove-Item -LiteralPath $symlinkPayloadPath -Force
        $manifestIndexEntry = '120000,' + $symlinkBlob + ',release/manifests/' + $TagName + '.json'
      } else {
        $manifestText | Set-Content -LiteralPath $manifestPath -Encoding utf8
      }
    }
    if ($AddNonProvenancePath) {
      Set-Content -LiteralPath (Join-Path $root 'unexpected.txt') -Value 'not provenance' -NoNewline -Encoding utf8
    }
    if (-not $MissingManifest -and -not $ManifestSymlink) {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('add', '--', ('release/manifests/' + $TagName + '.json')) -FailurePrefix 'Self-test provenance manifest add failed'
    }
    if ($AddNonProvenancePath) {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('add', '--', 'unexpected.txt') -FailurePrefix 'Self-test non-provenance add failed'
    }
    if ($null -ne $manifestIndexEntry) {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('update-index', '--add', '--cacheinfo', $manifestIndexEntry) -FailurePrefix 'Self-test symlink manifest index update failed'
    }
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('commit', '--allow-empty', '-m', 'release provenance') -FailurePrefix 'Self-test provenance commit failed'
    $targetCommit = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Self-test provenance commit lookup failed'
    if ($MultipleParents) {
      $tree = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', ($targetCommit + '^{tree}')) -FailurePrefix 'Self-test provenance tree lookup failed'
      $targetCommit = Get-GitSingleLine -RepositoryPath $root `
        -Arguments @('commit-tree', $tree, '-p', $targetCommit, '-p', $sourceCommit, '-m', 'merge provenance') `
        -FailurePrefix 'Self-test merge provenance commit failed'
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('update-ref', 'refs/heads/main', $targetCommit) -FailurePrefix 'Self-test merge provenance ref update failed'
    }
    if ($ZeroParents) {
      $targetCommit = $sourceCommit
    }
    $annotatedTagMessage = if ($HeaderLikeTagMessage) { 'object ' + ('0' * 40) + [char]10 + 'type commit' } else { 'source-only provenance' }
    if ($NestedAnnotatedTag) {
      $nestedTagName = $TagName + '-inner'
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', '-a', $nestedTagName, '-m', 'nested source-only provenance', $targetCommit) -FailurePrefix 'Self-test nested annotated tag failed'
      $nestedTagObject = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', ('refs/tags/' + $nestedTagName)) -FailurePrefix 'Self-test nested tag object lookup failed'
      $outerTagObjectPath = Join-Path $root '.nested-annotated-tag-object'
      $lineFeed = [char]10
      $outerTagObjectText = 'object ' + $nestedTagObject + $lineFeed + 'type tag' + $lineFeed + 'tag ' + $TagName + $lineFeed + 'tagger Hibiki self-test <hibiki-self-test@example.invalid> 0 +0000' + $lineFeed + $lineFeed + 'nested source-only provenance'
      Set-Content -LiteralPath $outerTagObjectPath -Value $outerTagObjectText -NoNewline -Encoding utf8
      $outerTagObject = Get-GitSingleLine -RepositoryPath $root -Arguments @('hash-object', '-w', '-t', 'tag', '--', $outerTagObjectPath) -FailurePrefix 'Self-test outer nested tag object failed'
      Remove-Item -LiteralPath $outerTagObjectPath -Force
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('update-ref', ('refs/tags/' + $TagName), $outerTagObject) -FailurePrefix 'Self-test outer nested tag ref update failed'
    } elseif ($LightweightTag) {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', $TagName, $targetCommit) -FailurePrefix 'Self-test lightweight tag failed'
    } else {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', '-a', $TagName, '-m', $annotatedTagMessage, $targetCommit) -FailurePrefix 'Self-test annotated tag failed'
    }
    return [pscustomobject]@{ Root = $root; SourceCommit = $sourceCommit; TargetCommit = $targetCommit }
  } catch {
    if (Test-Path -LiteralPath $root) { Remove-ReleaseProvenanceFixture -Root $root }
    throw
  }
}

function Remove-ReleaseProvenanceFixture {
  param([Parameter(Mandatory)][string]$Root)
  $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar))
  $tempPrefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
  $fullRoot = [IO.Path]::GetFullPath($Root)
  if (-not $fullRoot.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
      -not ([IO.Path]::GetFileName($fullRoot).StartsWith('hibiki-release-provenance-', [StringComparison]::Ordinal))) {
    throw "Refusing to remove unexpected self-test path '$Root'."
  }
  if (Test-Path -LiteralPath $fullRoot) { Remove-Item -LiteralPath $fullRoot -Recurse -Force }
}

if ($SelfTest) {
  function Assert-ProvenanceRejected {
    param(
      [Parameter(Mandatory)][scriptblock]$Action,
      [Parameter(Mandatory)][string]$ExpectedPattern,
      [Parameter(Mandatory)][string]$Label
    )
    try { & $Action } catch {
      if ($_.Exception.Message -notmatch $ExpectedPattern) {
        throw "release-provenance-check self-test '$Label' had unexpected error: $($_.Exception.Message)"
      }
      return
    }
    throw "release-provenance-check self-test '$Label' expected rejection."
  }

  $caseCount = 0
  $valid = New-ReleaseProvenanceFixture -TagName 'v1.2.3' -HeaderLikeTagMessage
  try {
    $result = Test-ReleaseTagProvenance -RepositoryPath $valid.Root -TagName 'v1.2.3'
    if ($result.tag -cne 'v1.2.3' -or $result.source_commit -cne $valid.SourceCommit -or
        $result.tag_commit -cne $valid.TargetCommit -or $result.manifest_path -cne 'release/manifests/v1.2.3.json') {
      throw 'release-provenance-check self-test valid annotated tag returned an unexpected result.'
    }
    $caseCount++

    Assert-ProvenanceRejected -Label 'invalid-git-ref' -ExpectedPattern 'valid Git tag ref' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $valid.Root -TagName 'v1.2.3:invalid' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $valid.Root
  }

  $lightweight = New-ReleaseProvenanceFixture -TagName 'v1.2.4' -LightweightTag
  try {
    Assert-ProvenanceRejected -Label 'lightweight-tag' -ExpectedPattern 'must be annotated' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $lightweight.Root -TagName 'v1.2.4' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $lightweight.Root
  }

  $missing = New-ReleaseProvenanceFixture -TagName 'v1.2.5' -MissingManifest
  try {
    Assert-ProvenanceRejected -Label 'missing-manifest' -ExpectedPattern 'is missing text manifest' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $missing.Root -TagName 'v1.2.5' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $missing.Root
  }

  $malformed = New-ReleaseProvenanceFixture -TagName 'v1.2.5-m' -MalformedManifest
  try {
    Assert-ProvenanceRejected -Label 'malformed-release-manifest' -ExpectedPattern 'must satisfy SourceReleaseManifest v1 schema' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $malformed.Root -TagName 'v1.2.5-m' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $malformed.Root
  }

  $mismatchedTag = New-ReleaseProvenanceFixture -TagName 'v1.2.6' -ManifestTag 'v9.9.9'
  try {
    Assert-ProvenanceRejected -Label 'manifest-tag-mismatch' -ExpectedPattern 'source_tag.*does not match tag' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $mismatchedTag.Root -TagName 'v1.2.6' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $mismatchedTag.Root
  }

  $mismatchedCommit = New-ReleaseProvenanceFixture -TagName 'v1.2.7' -ManifestCommit ('0' * 40)
  try {
    Assert-ProvenanceRejected -Label 'manifest-commit-mismatch' -ExpectedPattern 'source_commit does not match' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $mismatchedCommit.Root -TagName 'v1.2.7' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $mismatchedCommit.Root
  }

  $nonStringCommit = New-ReleaseProvenanceFixture -TagName 'v1.2.8' -ManifestCommit 123
  try {
    Assert-ProvenanceRejected -Label 'manifest-commit-non-string' -ExpectedPattern 'must satisfy SourceReleaseManifest v1 schema' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $nonStringCommit.Root -TagName 'v1.2.8' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $nonStringCommit.Root
  }

  $nonProvenance = New-ReleaseProvenanceFixture -TagName 'v1.2.9' -AddNonProvenancePath
  try {
    Assert-ProvenanceRejected -Label 'non-provenance-change' -ExpectedPattern 'may change only' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $nonProvenance.Root -TagName 'v1.2.9' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $nonProvenance.Root
  }

  $mergeTarget = New-ReleaseProvenanceFixture -TagName 'v1.3.0' -MultipleParents
  try {
    Assert-ProvenanceRejected -Label 'multiple-parents' -ExpectedPattern 'single-parent provenance metadata commit' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $mergeTarget.Root -TagName 'v1.3.0' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $mergeTarget.Root
  }

  $zeroParent = New-ReleaseProvenanceFixture -TagName 'v1.3.1' -ZeroParents
  try {
    Assert-ProvenanceRejected -Label 'zero-parent-target' -ExpectedPattern 'single-parent provenance metadata commit' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $zeroParent.Root -TagName 'v1.3.1' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $zeroParent.Root
  }

  $symlinkManifest = New-ReleaseProvenanceFixture -TagName 'v1.3.2' -ManifestSymlink
  try {
    Assert-ProvenanceRejected -Label 'symlink-manifest' -ExpectedPattern 'must be a regular 100644 blob' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $symlinkManifest.Root -TagName 'v1.3.2' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $symlinkManifest.Root
  }

  $nestedTag = New-ReleaseProvenanceFixture -TagName 'v1.3.3' -NestedAnnotatedTag
  try {
    Assert-ProvenanceRejected -Label 'nested-annotated-tag' -ExpectedPattern 'must directly target a commit' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $nestedTag.Root -TagName 'v1.3.3' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $nestedTag.Root
  }

  $headerLikeMessage = New-ReleaseProvenanceFixture -TagName 'v1.3.4' -HeaderLikeTagMessage
  try {
    $result = Test-ReleaseTagProvenance -RepositoryPath $headerLikeMessage.Root -TagName 'v1.3.4'
    if ($result.tag_commit -cne $headerLikeMessage.TargetCommit -or
        $result.source_commit -cne $headerLikeMessage.SourceCommit) {
      throw 'release-provenance-check self-test header-like tag message returned an unexpected result.'
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $headerLikeMessage.Root
  }

  $corruptHash = New-ReleaseProvenanceFixture -TagName 'v1.3.5' -CorruptedFileHash
  try {
    Assert-ProvenanceRejected -Label 'corrupted-file-hash' -ExpectedPattern 'hash mismatch' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $corruptHash.Root -TagName 'v1.3.5' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $corruptHash.Root
  }

  $duplicatePath = New-ReleaseProvenanceFixture -TagName 'v1.3.6' -DuplicateFilePath
  try {
    Assert-ProvenanceRejected -Label 'duplicate-path' -ExpectedPattern 'duplicate declared path' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $duplicatePath.Root -TagName 'v1.3.6' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $duplicatePath.Root
  }

  $missingFile = New-ReleaseProvenanceFixture -TagName 'v1.3.7' -MissingSourceFile
  try {
    Assert-ProvenanceRejected -Label 'missing-source-file' -ExpectedPattern 'does not exist in commit' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $missingFile.Root -TagName 'v1.3.7' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $missingFile.Root
  }

  $wrongProductVersion = New-ReleaseProvenanceFixture -TagName 'v1.3.8' -WrongProductVersion
  try {
    Assert-ProvenanceRejected -Label 'product-version-mismatch' -ExpectedPattern 'product_version.*does not match tag version' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $wrongProductVersion.Root -TagName 'v1.3.8' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $wrongProductVersion.Root
  }

  $wrongDistributionId = New-ReleaseProvenanceFixture -TagName 'v1.3.9' -WrongDistributionId
  try {
    Assert-ProvenanceRejected -Label 'distribution-id-mismatch' -ExpectedPattern 'distribution_id.*does not match config/distribution-profile' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $wrongDistributionId.Root -TagName 'v1.3.9' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $wrongDistributionId.Root
  }

  $publishedArtifact = New-ReleaseProvenanceFixture -TagName 'v1.4.0' -PublishedArtifact
  try {
    Assert-ProvenanceRejected -Label 'published-artifact-status' -ExpectedPattern 'must satisfy SourceReleaseManifest v1 schema' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $publishedArtifact.Root -TagName 'v1.4.0' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $publishedArtifact.Root
  }

  $malformedSbom = New-ReleaseProvenanceFixture -TagName 'v1.4.1' -MalformedSbom
  try {
    Assert-ProvenanceRejected -Label 'malformed-spdx-sbom' -ExpectedPattern 'must contain non-empty SPDXID' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $malformedSbom.Root -TagName 'v1.4.1' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $malformedSbom.Root
  }

  $emptyReleaseNotes = New-ReleaseProvenanceFixture -TagName 'v1.4.1-notes' -EmptyReleaseNotes
  try {
    Assert-ProvenanceRejected -Label 'empty-release-notes' -ExpectedPattern 'must not be empty' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $emptyReleaseNotes.Root -TagName 'v1.4.1-notes' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $emptyReleaseNotes.Root
  }

  $emptySourceFiles = New-ReleaseProvenanceFixture -TagName 'v1.4.2' -EmptySourceFiles
  try {
    Assert-ProvenanceRejected -Label 'empty-source-files' -ExpectedPattern 'must satisfy SourceReleaseManifest v1 schema' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $emptySourceFiles.Root -TagName 'v1.4.2' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $emptySourceFiles.Root
  }

  $emptyTests = New-ReleaseProvenanceFixture -TagName 'v1.4.3' -EmptyTests
  try {
    Assert-ProvenanceRejected -Label 'empty-tests' -ExpectedPattern 'must satisfy SourceReleaseManifest v1 schema' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $emptyTests.Root -TagName 'v1.4.3' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $emptyTests.Root
  }

  $unsafeSourcePath = New-ReleaseProvenanceFixture -TagName 'v1.4.4' -UnsafeSourceFilePath
  try {
    Assert-ProvenanceRejected -Label 'unsafe-source-path' -ExpectedPattern 'is not safe' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $unsafeSourcePath.Root -TagName 'v1.4.4' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $unsafeSourcePath.Root
  }

  $sourceSymlink = New-ReleaseProvenanceFixture -TagName 'v1.4.5' -SourceFileSymlink
  try {
    Assert-ProvenanceRejected -Label 'source-file-symlink' -ExpectedPattern 'must be a regular 100644 blob' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $sourceSymlink.Root -TagName 'v1.4.5' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $sourceSymlink.Root
  }

  $duplicateRequiredPath = New-ReleaseProvenanceFixture -TagName 'v1.4.6' -DuplicateRequiredFilePath
  try {
    Assert-ProvenanceRejected -Label 'duplicate-required-path' -ExpectedPattern 'duplicate declared path' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $duplicateRequiredPath.Root -TagName 'v1.4.6' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $duplicateRequiredPath.Root
  }

  $wrongRolePath = New-ReleaseProvenanceFixture -TagName 'v1.4.7' -WrongRolePath
  try {
    Assert-ProvenanceRejected -Label 'wrong-role-path' -ExpectedPattern 'toolchain_lock\.path must be' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $wrongRolePath.Root -TagName 'v1.4.7' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $wrongRolePath.Root
  }

  $tempSibling = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)) + 'Sibling' + [IO.Path]::DirectorySeparatorChar + 'hibiki-release-provenance-test'
  Assert-ProvenanceRejected -Label 'temp-prefix-sibling' -ExpectedPattern 'Refusing to remove unexpected self-test path' -Action {
    Remove-ReleaseProvenanceFixture -Root $tempSibling
  }
  $caseCount++

  Write-Output "Release provenance gate self-test passed ($caseCount cases)."
  exit 0
}

if ([string]::IsNullOrWhiteSpace($Repository)) {
  $Repository = Split-Path -Parent $PSScriptRoot
}
if ([string]::IsNullOrWhiteSpace($Tag)) {
  throw 'Tag is required. Pass -Tag <vX.Y.Z> from an annotated source-tag workflow.'
}

$result = Test-ReleaseTagProvenance -RepositoryPath $Repository -TagName $Tag
Write-Output ("Release provenance passed: tag=" + $result.tag + " tag_commit=" + $result.tag_commit +
  " source_commit=" + $result.source_commit + " manifest=" + $result.manifest_path)
