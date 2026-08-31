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
$script:SchemaPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'schemas/release-manifest-v1.schema.json'
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
    throw "Release tag '$TagName' must match the ReleaseManifest source_tag format."
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
    throw "ReleaseManifest v1 schema is missing: $SchemaFile"
  }
  try {
    if (-not $script:SchemaDependenciesRegistered) {
      $printableSchema = Join-Path (Split-Path -Parent $SchemaFile) 'printable-string-v1.schema.json'
      if (-not (Test-Path -LiteralPath $printableSchema -PathType Leaf)) {
        throw "ReleaseManifest printable-string schema is missing: $printableSchema"
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
    throw "Release manifest '$ManifestPath' must satisfy ReleaseManifest v1 schema."
  }
}

function Get-DirectAnnotatedTagCommit {
  param(
    [Parameter(Mandatory)][string]$RepositoryPath,
    [Parameter(Mandatory)][string]$TagName,
    [Parameter(Mandatory)][string]$TagRef
  )

  $tagHeaders = @(Invoke-GitText -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-p', $TagRef) `
    -FailurePrefix "Release tag '$TagName' cannot read its annotated tag object")
  $objectHeaders = @($tagHeaders | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^object [0-9a-f]{40}$' })
  $typeHeaders = @($tagHeaders | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^type [A-Za-z]+$' })
  if ($objectHeaders.Count -ne 1 -or $typeHeaders.Count -ne 1) {
    throw "Release tag '$TagName' must contain one direct object and type header."
  }
  if ($typeHeaders[0] -cne 'type commit') {
    throw "Release tag '$TagName' must directly target a commit; nested tags are rejected."
  }

  $tagCommit = $objectHeaders[0].Substring('object '.Length)
  $targetObjectType = Get-GitSingleLine -RepositoryPath $RepositoryPath `
    -Arguments @('cat-file', '-t', $tagCommit) `
    -FailurePrefix "Release tag '$TagName' direct target cannot be read"
  if ($targetObjectType -cne 'commit') {
    throw "Release tag '$TagName' must directly target a commit."
  }
  return $tagCommit
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
  $expectedChange = '^[AM]' + [char]9 + [regex]::Escape($manifestPath) + '$'
  if ($changedPaths.Count -ne 1 -or $changedPaths[0] -notmatch $expectedChange) {
    throw "Release tag '$TagName' provenance metadata commit may change only '$manifestPath'."
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
    [switch]$NestedAnnotatedTag
  )

  $root = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-release-provenance-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $root -Force | Out-Null
  try {
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('init', '--initial-branch', 'main') -FailurePrefix 'Self-test git init failed'
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('config', 'user.name', 'Hibiki self-test') -FailurePrefix 'Self-test git user.name setup failed'
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('config', 'user.email', 'hibiki-self-test@example.invalid') -FailurePrefix 'Self-test git user.email setup failed'

    New-Item -ItemType Directory -Path (Join-Path $root 'source') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $root 'source/payload.txt') -Value 'source-only fixture' -NoNewline -Encoding utf8
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('add', '--all') -FailurePrefix 'Self-test source add failed'
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('commit', '-m', 'source input') -FailurePrefix 'Self-test source commit failed'
    $sourceCommit = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', 'HEAD') -FailurePrefix 'Self-test source commit lookup failed'

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
        $manifestValue = [ordered]@{
          schema_version = 1
          product_version = '1.0.0'
          source_tag = if (-not $PSBoundParameters.ContainsKey('ManifestTag')) { $TagName } else { $ManifestTag }
          source_commit = if (-not $PSBoundParameters.ContainsKey('ManifestCommit')) { $sourceCommit } else { $ManifestCommit }
          distribution_id = 'hibiki-self-test'
          toolchain_digest = 'a' * 64
          dependency_lock_digest = 'b' * 64
          unsigned_files = @([ordered]@{ path = 'source/payload.txt'; sha256 = 'c' * 64 })
          driver_package = [ordered]@{ sha256 = 'd' * 64; catalog_sha256 = 'e' * 64 }
          installer = [ordered]@{ sha256 = 'f' * 64 }
          sbom_digest = '0' * 64
          tests = @('self-test')
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
    $null = Invoke-GitText -RepositoryPath $root -Arguments @('add', '--all') -FailurePrefix 'Self-test provenance add failed'
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
    if ($NestedAnnotatedTag) {
      $nestedTagName = $TagName + '-inner'
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', '-a', $nestedTagName, '-m', 'nested source-only provenance') -FailurePrefix 'Self-test nested annotated tag failed'
      $nestedTagObject = Get-GitSingleLine -RepositoryPath $root -Arguments @('rev-parse', ('refs/tags/' + $nestedTagName)) -FailurePrefix 'Self-test nested tag object lookup failed'
      $outerTagObjectPath = Join-Path $root '.nested-annotated-tag-object'
      $lineFeed = [char]10
      $outerTagObjectText = 'object ' + $nestedTagObject + $lineFeed + 'type tag' + $lineFeed + 'tag ' + $TagName + $lineFeed + 'tagger Hibiki self-test <hibiki-self-test@example.invalid> 0 +0000' + $lineFeed + $lineFeed + 'nested source-only provenance'
      Set-Content -LiteralPath $outerTagObjectPath -Value $outerTagObjectText -NoNewline -Encoding utf8
      $outerTagObject = Get-GitSingleLine -RepositoryPath $root -Arguments @('hash-object', '-w', '-t', 'tag', '--', $outerTagObjectPath) -FailurePrefix 'Self-test outer nested tag object failed'
      Remove-Item -LiteralPath $outerTagObjectPath -Force
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('update-ref', ('refs/tags/' + $TagName), $outerTagObject) -FailurePrefix 'Self-test outer nested tag ref update failed'
    } elseif ($LightweightTag) {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', $TagName) -FailurePrefix 'Self-test lightweight tag failed'
    } else {
      $null = Invoke-GitText -RepositoryPath $root -Arguments @('tag', '-a', $TagName, '-m', 'source-only provenance') -FailurePrefix 'Self-test annotated tag failed'
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
  $valid = New-ReleaseProvenanceFixture -TagName 'v1.2.3'
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
    Assert-ProvenanceRejected -Label 'malformed-release-manifest' -ExpectedPattern 'must satisfy ReleaseManifest v1 schema' -Action {
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
    Assert-ProvenanceRejected -Label 'manifest-commit-non-string' -ExpectedPattern 'must satisfy ReleaseManifest v1 schema' -Action {
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

  $symlinkManifest = New-ReleaseProvenanceFixture -TagName 'v1.3.1' -ManifestSymlink
  try {
    Assert-ProvenanceRejected -Label 'symlink-manifest' -ExpectedPattern 'must be a regular 100644 blob' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $symlinkManifest.Root -TagName 'v1.3.1' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $symlinkManifest.Root
  }

  $nestedTag = New-ReleaseProvenanceFixture -TagName 'v1.3.2' -NestedAnnotatedTag
  try {
    Assert-ProvenanceRejected -Label 'nested-annotated-tag' -ExpectedPattern 'must directly target a commit' -Action {
      Test-ReleaseTagProvenance -RepositoryPath $nestedTag.Root -TagName 'v1.3.2' | Out-Null
    }
    $caseCount++
  } finally {
    Remove-ReleaseProvenanceFixture -Root $nestedTag.Root
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
