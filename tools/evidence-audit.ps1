#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest,
  [switch]$DescribeCurrentChange,
  [ValidateSet('auto', 'pull_request', 'push', 'merge_group', 'workflow_dispatch')]
  [string]$CandidateKind = 'auto'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$script:SchemaPath = Join-Path $repo 'schemas/evidence-manifest-v2.schema.json'
$script:DigestAlgorithm = 'sha256-git-source-set-v1'
$script:MaxPaths = 512
$script:MaxPathLength = 240
$script:MaxSnapshotBytes = 134217728L
$script:BlobInfoCache = @{}

function Invoke-GitText {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$GitArgs,
    [switch]$AllowFailure
  )

  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = 'git'
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  foreach ($argument in @('-C', $Repository, '--literal-pathspecs') + $GitArgs) { $start.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $start
  if (-not $process.Start()) { throw 'Unable to start git.' }
  try {
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -and -not $AllowFailure) {
      $detail = $stderr.Trim()
      if ([string]::IsNullOrWhiteSpace($detail)) { $detail = $stdout.Trim() }
      throw "git $($GitArgs -join ' ') failed with exit $($process.ExitCode): $detail"
    }
    if ($process.ExitCode -ne 0 -or [string]::IsNullOrEmpty($stdout)) { return @() }
    return @($stdout -split "`r?`n" | Where-Object { $_.Length -gt 0 })
  } finally {
    $process.Dispose()
  }
}

function Test-GitCommand {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string[]]$GitArgs
  )
  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = 'git'
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  foreach ($argument in @('-C', $Repository, '--literal-pathspecs') + $GitArgs) { $start.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::Start($start)
  try {
    $process.StandardOutput.ReadToEnd() | Out-Null
    $process.StandardError.ReadToEnd() | Out-Null
    $process.WaitForExit()
    return ($process.ExitCode -eq 0)
  } finally {
    $process.Dispose()
  }
}

function Get-GitCommit {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$Ref)
  $lines = @(Invoke-GitText -Repository $Repository -GitArgs @('rev-parse', '--verify', "$Ref^{commit}"))
  if ($lines.Count -ne 1 -or $lines[0] -notmatch '^[0-9a-f]{40,64}$') {
    throw "Git ref '$Ref' did not resolve to exactly one commit."
  }
  return $lines[0]
}

function Test-GitAncestor {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$Ancestor,
    [Parameter(Mandatory)] [string]$Descendant
  )
  if ($Ancestor -notmatch '^[0-9a-f]{40,64}$' -or $Descendant -notmatch '^[0-9a-f]{40,64}$') { return $false }
  return (Test-GitCommand -Repository $Repository -GitArgs @('merge-base', '--is-ancestor', $Ancestor, $Descendant))
}

function Get-OrdinalSortedPaths {
  param([Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Paths)
  $copy = [string[]]@($Paths)
  [Array]::Sort($copy, [StringComparer]::Ordinal)
  return $copy
}

function Test-IsEvidencePath {
  param([Parameter(Mandatory)] [string]$Path)
  return ($Path -match '^(?i:evidence)(?:/|$)')
}

function Get-PathListFinding {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Paths,
    [switch]$AllowEmpty,
    [switch]$AllowEvidence
  )

  if ($Paths.Count -eq 0 -and -not $AllowEmpty) { return 'V2_PATHS_EMPTY' }
  if ($Paths.Count -gt $script:MaxPaths) { return 'V2_PATHS_LIMIT' }
  $caseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($path in $Paths) {
    if ([string]::IsNullOrWhiteSpace($path) -or $path.Length -gt $script:MaxPathLength) { return 'V2_PATH_FORMAT' }
    if ($path -notmatch '^[\x20-\x7E]+$') { return 'V2_PATH_FORMAT' }
    if ($path.Contains('\') -or $path.Contains(':') -or $path.IndexOfAny([char[]]'*?[]') -ge 0) { return 'V2_PATH_FORMAT' }
    if ($path.StartsWith('/') -or $path.EndsWith('/') -or $path.Contains('//')) { return 'V2_PATH_FORMAT' }
    foreach ($segment in $path.Split('/')) {
      if ($segment -eq '.' -or $segment -eq '..' -or $segment.Length -eq 0) { return 'V2_PATH_TRAVERSAL' }
    }
    if ($path -match '^(?i:\.git)(?:/|$)') { return 'V2_PATH_GIT_INTERNAL' }
    if (-not $AllowEvidence -and (Test-IsEvidencePath -Path $path)) { return 'V2_PATH_EVIDENCE' }
    if (-not $caseFolded.Add($path)) { return 'V2_PATH_DUPLICATE' }
  }
  $sorted = @(Get-OrdinalSortedPaths -Paths $Paths)
  for ($index = 0; $index -lt $Paths.Count; $index++) {
    if ($Paths[$index] -cne $sorted[$index]) { return 'V2_PATHS_UNSORTED' }
  }
  return $null
}

function Get-ProvenanceFinding {
  param(
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$FileHistory,
    [Parameter(Mandatory)] [bool]$CommitExists,
    [Parameter(Mandatory)] [bool]$ReachableFromMain
  )
  if ($null -eq $Record.PSObject.Properties['source_commit']) { return 'MISSING_FIELD' }
  $hash = [string]$Record.source_commit
  if ([string]::IsNullOrWhiteSpace($hash)) { return 'EMPTY' }
  if ($hash.Length -ne 40 -or $hash -notmatch '^[0-9a-f]{40}$') { return 'FORMAT' }
  if (-not $CommitExists) { return 'NOT_FOUND' }
  if (-not $ReachableFromMain) { return 'UNREACHABLE' }
  if (@($FileHistory) -notcontains $hash) { return 'NOT_FILE_HISTORY' }
  return $null
}

function Get-ProvenanceV2ShapeFinding {
  param([Parameter(Mandatory)] [psobject]$Record)
  if ($null -ne $Record.PSObject.Properties['source_commit']) { return 'V2_SOURCE_COMMIT_FORBIDDEN' }
  if ($null -ne $Record.PSObject.Properties['schema_version']) { return 'V2_SCHEMA_VERSION_FORBIDDEN' }
  if ($null -eq $Record.PSObject.Properties['evidence_format'] -or $Record.evidence_format -ne 2) { return 'V2_FORMAT' }
  if ($null -eq $Record.PSObject.Properties['source_provenance'] -or $null -eq $Record.source_provenance) {
    return 'V2_PROVENANCE_MISSING'
  }
  $provenance = $Record.source_provenance
  foreach ($name in @('mode', 'paths', 'digest_algorithm', 'digest')) {
    if ($null -eq $provenance.PSObject.Properties[$name]) { return "V2_$($name.ToUpperInvariant())_MISSING" }
  }
  $mode = [string]$provenance.mode
  if ($mode -notin @('change', 'snapshot')) { return 'V2_MODE' }
  if ($mode -eq 'snapshot') {
    if ($null -eq $provenance.PSObject.Properties['snapshot_commit'] -or
        ([string]$provenance.snapshot_commit).Length -ne 40 -or
        [string]$provenance.snapshot_commit -notmatch '^[0-9a-f]{40}$') { return 'V2_SNAPSHOT_COMMIT' }
  } elseif ($null -ne $provenance.PSObject.Properties['snapshot_commit']) {
    return 'V2_CHANGE_SNAPSHOT_COMMIT_FORBIDDEN'
  }
  if ([string]$provenance.digest_algorithm -cne $script:DigestAlgorithm) { return 'V2_DIGEST_ALGORITHM' }
  if (([string]$provenance.digest).Length -ne 64 -or [string]$provenance.digest -notmatch '^[0-9a-f]{64}$') { return 'V2_DIGEST_FORMAT' }
  return (Get-PathListFinding -Paths ([string[]]@($provenance.paths | ForEach-Object { [string]$_ })))
}

function Compare-ProvenanceV2Snapshot {
  param(
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$ExpectedPaths,
    [Parameter(Mandatory)] [string]$ExpectedDigest
  )
  $actualPaths = [string[]]@($Record.source_provenance.paths | ForEach-Object { [string]$_ })
  if ($actualPaths.Count -ne $ExpectedPaths.Count) { return 'V2_PATH_SET' }
  for ($index = 0; $index -lt $actualPaths.Count; $index++) {
    if ($actualPaths[$index] -cne $ExpectedPaths[$index]) { return 'V2_PATH_SET' }
  }
  if ([string]$Record.source_provenance.digest -cne $ExpectedDigest) { return 'V2_DIGEST_MISMATCH' }
  return $null
}

function Get-FirstDuplicateJsonProperty {
  param([Parameter(Mandatory)] [Text.Json.JsonElement]$Element, [string]$Pointer = '$')
  if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($property in $Element.EnumerateObject()) {
      if (-not $names.Add($property.Name)) { return "$Pointer.$($property.Name)" }
    }
    foreach ($property in $Element.EnumerateObject()) {
      $nested = Get-FirstDuplicateJsonProperty -Element $property.Value -Pointer "$Pointer.$($property.Name)"
      if ($null -ne $nested) { return $nested }
    }
  } elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
    $index = 0
    foreach ($item in $Element.EnumerateArray()) {
      $nested = Get-FirstDuplicateJsonProperty -Element $item -Pointer "$Pointer[$index]"
      if ($null -ne $nested) { return $nested }
      $index++
    }
  }
  return $null
}

function Get-DuplicateJsonProperty {
  param([Parameter(Mandatory)] [string]$Json)
  $document = [Text.Json.JsonDocument]::Parse($Json)
  try { return (Get-FirstDuplicateJsonProperty -Element $document.RootElement) }
  finally { $document.Dispose() }
}

function Get-GitBlobInfo {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$ObjectId)
  if ($ObjectId -notmatch '^[0-9a-f]{40,64}$') { throw "Invalid Git object id '$ObjectId'." }
  $cacheKey = "$Repository`0$ObjectId"
  if ($script:BlobInfoCache.ContainsKey($cacheKey)) { return $script:BlobInfoCache[$cacheKey] }
  $start = [Diagnostics.ProcessStartInfo]::new()
  $start.FileName = 'git'
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true
  foreach ($argument in @('-C', $Repository, '--literal-pathspecs', 'cat-file', 'blob', $ObjectId)) { $start.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::new()
  $process.StartInfo = $start
  if (-not $process.Start()) { throw 'Unable to start git cat-file.' }
  $hasher = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
  $buffer = [byte[]]::new(65536)
  $size = 0L
  try {
    while (($read = $process.StandardOutput.BaseStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
      $hasher.AppendData($buffer, 0, $read)
      $size += $read
      if ($size -gt $script:MaxSnapshotBytes) { throw "Git blob '$ObjectId' exceeds the evidence snapshot byte limit." }
    }
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "git cat-file failed for '$ObjectId': $($stderr.Trim())" }
    $result = [pscustomobject]@{
      Size = $size
      Sha256 = [Convert]::ToHexString($hasher.GetHashAndReset()).ToLowerInvariant()
    }
    $script:BlobInfoCache[$cacheKey] = $result
    return $result
  } finally {
    $hasher.Dispose()
    $process.Dispose()
  }
}

function Get-GitTreeEntry {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$Commit,
    [Parameter(Mandatory)] [string]$Path,
    [switch]$AllowDeleted
  )
  if ($Commit -notmatch '^[0-9a-f]{40,64}$') { throw "Invalid commit '$Commit'." }
  $lines = @(Invoke-GitText -Repository $Repository -GitArgs @('ls-tree', $Commit, '--', $Path))
  if ($lines.Count -eq 0) {
    if ($AllowDeleted) { return [pscustomobject]@{ Path = $Path; State = 'D'; Mode = ''; ObjectId = ''; Size = 0L; Sha256 = '' } }
    throw "Snapshot path '$Path' does not exist at commit '$Commit'."
  }
  if ($lines.Count -ne 1 -or $lines[0] -notmatch '^(?<mode>[0-9]{6}) (?<type>[a-z]+) (?<oid>[0-9a-f]{40,64})\t') {
    throw "Unable to parse Git tree entry for '$Path' at '$Commit'."
  }
  $mode = $Matches['mode']; $type = $Matches['type']; $objectId = $Matches['oid']
  if ($type -ne 'blob' -or $mode -notin @('100644', '100755')) {
    throw "Snapshot path '$Path' must be a regular Git blob, not $type mode $mode."
  }
  $blob = Get-GitBlobInfo -Repository $Repository -ObjectId $objectId
  return [pscustomobject]@{ Path = $Path; State = 'F'; Mode = $mode; ObjectId = $objectId; Size = [long]$blob.Size; Sha256 = [string]$blob.Sha256 }
}

function Get-GitIndexEntry {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$Path,
    [switch]$AllowDeleted
  )
  $lines = @(Invoke-GitText -Repository $Repository -GitArgs @('ls-files', '--stage', '--', $Path))
  if ($lines.Count -eq 0) {
    if ($AllowDeleted) { return [pscustomobject]@{ Path = $Path; State = 'D'; Mode = ''; ObjectId = ''; Size = 0L; Sha256 = '' } }
    throw "Snapshot path '$Path' is not staged in the Git index."
  }
  if ($lines.Count -ne 1 -or $lines[0] -notmatch '^(?<mode>[0-9]{6}) (?<oid>[0-9a-f]{40,64}) (?<stage>[0-3])\t') {
    throw "Unable to parse Git index entry for '$Path'."
  }
  $mode = $Matches['mode']; $objectId = $Matches['oid']; $stage = $Matches['stage']
  if ($stage -ne '0') { throw "Snapshot path '$Path' has an unresolved index stage." }
  if ($mode -notin @('100644', '100755')) { throw "Snapshot path '$Path' must be a regular Git blob, not mode $mode." }
  $blob = Get-GitBlobInfo -Repository $Repository -ObjectId $objectId
  return [pscustomobject]@{ Path = $Path; State = 'F'; Mode = $mode; ObjectId = $objectId; Size = [long]$blob.Size; Sha256 = [string]$blob.Sha256 }
}

function New-CanonicalSnapshot {
  param([Parameter(Mandatory)] [AllowEmptyCollection()] [psobject[]]$Entries)
  $sortedPaths = @(Get-OrdinalSortedPaths -Paths ([string[]]@($Entries | ForEach-Object { [string]$_.Path })))
  $pathFinding = Get-PathListFinding -Paths $sortedPaths -AllowEmpty
  if ($pathFinding) { throw $pathFinding }
  $byPath = @{}
  foreach ($entry in $Entries) {
    if ($byPath.ContainsKey([string]$entry.Path)) { throw 'V2_PATH_DUPLICATE' }
    $byPath[[string]$entry.Path] = $entry
  }
  $separator = [char]0
  $builder = [Text.StringBuilder]::new()
  [void]$builder.Append('hibiki-evidence-source-set-v1').Append($separator)
  $totalBytes = 0L
  foreach ($path in $sortedPaths) {
    $entry = $byPath[$path]
    if ($null -eq $entry.PSObject.Properties['Before'] -or $null -eq $entry.PSObject.Properties['After']) {
      throw "Snapshot entry '$path' must contain before and after states."
    }
    [void]$builder.Append('P').Append($separator).Append($path).Append($separator)
    foreach ($sideName in @('Before', 'After')) {
      $side = $entry.$sideName
      if ([string]$side.State -eq 'D') {
        [void]$builder.Append('0').Append($separator)
        continue
      }
      if ([string]$side.State -ne 'F' -or [string]$side.Mode -notin @('100644', '100755')) {
        throw "Unsupported $sideName state for '$path'."
      }
      if ([string]$side.Sha256 -notmatch '^[0-9a-f]{64}$' -or [long]$side.Size -lt 0) {
        throw "Invalid $sideName blob metadata for '$path'."
      }
      $totalBytes += [long]$side.Size
      if ($totalBytes -gt $script:MaxSnapshotBytes) { throw 'Evidence source snapshot exceeds the total byte limit.' }
      [void]$builder.Append('1').Append($separator).Append([string]$side.Mode).Append($separator)
      [void]$builder.Append(([long]$side.Size).ToString([Globalization.CultureInfo]::InvariantCulture)).Append($separator)
      [void]$builder.Append([string]$side.Sha256).Append($separator)
    }
  }
  $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($builder.ToString()))).ToLowerInvariant()
  return [pscustomobject]@{ Paths = [string[]]$sortedPaths; Digest = $digest; Entries = [psobject[]]$Entries }
}

function Get-NonEvidencePaths {
  param([Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Paths)
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach ($candidate in $Paths) {
    $path = $candidate -replace '\\', '/'
    if (-not (Test-IsEvidencePath -Path $path)) { [void]$seen.Add($path) }
  }
  return [string[]]@(Get-OrdinalSortedPaths -Paths ([string[]]@($seen)))
}

function Assert-IndexSnapshotReady {
  param([Parameter(Mandatory)] [string]$Repository)
  $unstaged = @(Get-NonEvidencePaths -Paths ([string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('diff', '--no-ext-diff', '--no-textconv', '--no-renames', '--ignore-submodules=none', '--name-only', '--'))))
  if ($unstaged.Count -gt 0) { throw "SOURCE_WORKTREE_UNSTAGED: $($unstaged -join ', ')" }
  $untracked = @(Get-NonEvidencePaths -Paths ([string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('ls-files', '--others', '--exclude-standard', '--'))))
  if ($untracked.Count -gt 0) { throw "SOURCE_WORKTREE_UNTRACKED: $($untracked -join ', ')" }
}

function Get-IndexChangePaths {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$BaseCommit)
  Assert-IndexSnapshotReady -Repository $Repository
  return (Get-NonEvidencePaths -Paths ([string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('diff', '--cached', '--no-ext-diff', '--no-textconv', '--no-renames', '--ignore-submodules=none', '--name-only', $BaseCommit, '--'))))
}

function Get-CommitChangePaths {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$BaseCommit,
    [Parameter(Mandatory)] [string]$TargetCommit
  )
  return (Get-NonEvidencePaths -Paths ([string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('diff', '--no-ext-diff', '--no-textconv', '--no-renames', '--ignore-submodules=none', '--name-only', $BaseCommit, $TargetCommit, '--'))))
}

function Get-IndexSnapshot {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$BaseCommit)
  $paths = @(Get-IndexChangePaths -Repository $Repository -BaseCommit $BaseCommit)
  $entries = [Collections.Generic.List[psobject]]::new()
  foreach ($path in $paths) {
    $entries.Add([pscustomobject]@{
      Path = $path
      Before = Get-GitTreeEntry -Repository $Repository -Commit $BaseCommit -Path $path -AllowDeleted
      After = Get-GitIndexEntry -Repository $Repository -Path $path -AllowDeleted
    })
  }
  return (New-CanonicalSnapshot -Entries ([psobject[]]$entries))
}

function Get-CommitSnapshot {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$BaseCommit,
    [Parameter(Mandatory)] [string]$TargetCommit
  )
  $paths = @(Get-CommitChangePaths -Repository $Repository -BaseCommit $BaseCommit -TargetCommit $TargetCommit)
  $entries = [Collections.Generic.List[psobject]]::new()
  foreach ($path in $paths) {
    $entries.Add([pscustomobject]@{
      Path = $path
      Before = Get-GitTreeEntry -Repository $Repository -Commit $BaseCommit -Path $path -AllowDeleted
      After = Get-GitTreeEntry -Repository $Repository -Commit $TargetCommit -Path $path -AllowDeleted
    })
  }
  return (New-CanonicalSnapshot -Entries ([psobject[]]$entries))
}

function Get-CommitFirstParent {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$Commit)
  $lines = @(Invoke-GitText -Repository $Repository -GitArgs @('rev-list', '--parents', '-n', '1', $Commit))
  if ($lines.Count -ne 1) { throw "Unable to resolve parents for '$Commit'." }
  $parts = @($lines[0].Split(' ', [StringSplitOptions]::RemoveEmptyEntries))
  if ($parts.Count -lt 2 -or $parts[1] -notmatch '^[0-9a-f]{40,64}$') { throw "Evidence commit '$Commit' has no first parent." }
  return $parts[1]
}

function Get-IntroductionCommit {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$HistoryRef,
    [Parameter(Mandatory)] [string]$Path,
    [AllowNull()] [string]$AfterCommit
  )
  $commits = [string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('log', '--first-parent', '--diff-filter=A', '--format=%H', $HistoryRef, '--', $Path))
  if (-not [string]::IsNullOrWhiteSpace($AfterCommit)) {
    $commits = [string[]]@($commits | Where-Object { (Test-GitAncestor -Repository $Repository -Ancestor $AfterCommit -Descendant $_) -and $_ -ne $AfterCommit })
  }
  if ($commits.Count -ne 1) { throw "Expected one first-parent introduction commit for '$Path', found $($commits.Count)." }
  return $commits[0]
}

function Get-LatestTouchCommit {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$HistoryRef,
    [Parameter(Mandatory)] [string]$Path
  )
  $commits = @(Invoke-GitText -Repository $Repository -GitArgs @('log', '--first-parent', '-n', '1', '--format=%H', $HistoryRef, '--', $Path))
  if ($commits.Count -ne 1 -or $commits[0] -notmatch '^[0-9a-f]{40,64}$') {
    throw "Unable to derive the latest first-parent touch for '$Path'."
  }
  return $commits[0]
}

function Test-PathAtCommit {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$Commit, [Parameter(Mandatory)] [string]$Path)
  return (@(Invoke-GitText -Repository $Repository -GitArgs @('ls-tree', $Commit, '--', $Path)).Count -gt 0)
}

function Test-V2SchemaText {
  param([Parameter(Mandatory)] [string]$Json)
  if (-not (Test-Path -LiteralPath $script:SchemaPath -PathType Leaf)) { return $false }
  try { return [bool](Test-Json -Json $Json -SchemaFile $script:SchemaPath -ErrorAction SilentlyContinue 2>$null) }
  catch { return $false }
}

function Get-V2TimeFinding {
  param([Parameter(Mandatory)] [psobject]$Record)
  try {
    $started = [DateTimeOffset]::Parse([string]$Record.metadata.started_at, [Globalization.CultureInfo]::InvariantCulture)
    $finished = [DateTimeOffset]::Parse([string]$Record.metadata.finished_at, [Globalization.CultureInfo]::InvariantCulture)
  } catch { return 'V2_TIME_FORMAT' }
  if ($finished -lt $started) { return 'V2_TIME_ORDER' }
  return $null
}

function Get-EvidenceChangePaths {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$BaseCommit)
  $tracked = [string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('diff', '--no-ext-diff', '--no-textconv', '--no-renames', '--ignore-submodules=none', '--name-only', $BaseCommit, '--', 'evidence'))
  $untracked = [string[]]@(Invoke-GitText -Repository $Repository -GitArgs @('ls-files', '--others', '--exclude-standard', '--', 'evidence'))
  $paths = @(@($tracked + $untracked) | ForEach-Object { $_ -replace '\\', '/' } | Where-Object { $_ -match '(?i)\.json$' })
  return [string[]]@(Get-OrdinalSortedPaths -Paths ([string[]]@($paths | Select-Object -Unique)))
}

function Get-IndexTrackedPathSet {
  param([Parameter(Mandatory)] [string]$Repository)
  $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach ($path in @(Invoke-GitText -Repository $Repository -GitArgs @('ls-files', '--', 'evidence'))) {
    [void]$set.Add(($path -replace '\\', '/'))
  }
  return ,$set
}

function Test-EvidenceDeletionState {
  param(
    [Parameter(Mandatory)] [bool]$ExistedAtBase,
    [Parameter(Mandatory)] [bool]$TrackedInIndex,
    [Parameter(Mandatory)] [bool]$ExistsInWorktree
  )
  if ($ExistedAtBase) { return ((-not $TrackedInIndex) -or (-not $ExistsInWorktree)) }
  return (-not $ExistsInWorktree)
}

function Get-TreeBlobMap {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$Commit)
  $map = [Collections.Generic.Dictionary[string,psobject]]::new([StringComparer]::Ordinal)
  foreach ($line in @(Invoke-GitText -Repository $Repository -GitArgs @('ls-tree', '-r', $Commit, '--', 'evidence'))) {
    if ($line -notmatch '^(?<mode>[0-9]{6}) (?<type>[a-z]+) (?<oid>[0-9a-f]{40,64})\t(?<path>.+)$') {
      throw "Unable to parse evidence tree entry at '$Commit'."
    }
    if ($Matches['type'] -ne 'blob') { continue }
    $path = $Matches['path'] -replace '\\', '/'
    $map[$path] = [pscustomobject]@{ Mode = $Matches['mode']; ObjectId = $Matches['oid'] }
  }
  return ,$map
}

function Get-PolicyCutoverCommit {
  param([Parameter(Mandatory)] [string]$Repository, [Parameter(Mandatory)] [string]$HistoryRef)
  $commits = @(Invoke-GitText -Repository $Repository -GitArgs @('log', '--first-parent', '--diff-filter=A', '--format=%H', $HistoryRef, '--', 'schemas/evidence-manifest-v2.schema.json'))
  if ($commits.Count -eq 0) { return $null }
  if ($commits.Count -ne 1) { throw "Expected one evidence v2 policy cutover commit, found $($commits.Count)." }
  return $commits[0]
}

function Get-EvidenceInventoryPaths {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [string]$PriorCommit,
    [Parameter(Mandatory)] [string]$HistoryRef
  )
  $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach ($path in @(Invoke-GitText -Repository $Repository -GitArgs @('log', '--first-parent', '--name-only', '--format=', "$PriorCommit..$HistoryRef", '--', 'evidence'))) {
    $relative = $path -replace '\\', '/'
    if ($relative -match '(?i)\.json$') { [void]$set.Add($relative) }
  }
  return ,$set
}

function Test-SupersedesTargets {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [string]$RecordPath,
    [Parameter(Mandatory)] [string]$PriorCommit
  )
  if ($null -eq $Record.PSObject.Properties['supersedes']) { return $null }
  foreach ($targetValue in @($Record.supersedes)) {
    $target = [string]$targetValue
    if ($target -ceq $RecordPath) { return 'V2_SUPERSEDES_SELF' }
    $finding = Get-PathListFinding -Paths @($target) -AllowEvidence
    if ($finding -or $target -notmatch '^(?i:evidence)/.+\.json$') { return 'V2_SUPERSEDES_PATH' }
    if (-not (Test-PathAtCommit -Repository $Repository -Commit $PriorCommit -Path $target)) { return 'V2_SUPERSEDES_NOT_FOUND' }
  }
  return $null
}

function Get-DuplicateSuccessorFinding {
  param(
    [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]]$Successors,
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [string]$RecordPath
  )
  if ($null -eq $Record.PSObject.Properties['supersedes']) { return $null }
  foreach ($targetValue in @($Record.supersedes)) {
    $target = [string]$targetValue
    if ($Successors.ContainsKey($target) -and $Successors[$target] -cne $RecordPath) {
      return "target=$target other=$($Successors[$target])"
    }
    $Successors[$target] = $RecordPath
  }
  return $null
}

function Get-V2SourceSetFinding {
  param(
    [Parameter(Mandatory)] [string]$Repository,
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [string]$PriorCommit,
    [AllowNull()] [string]$RecordCommit
  )
  $mode = [string]$Record.source_provenance.mode
  $useIndex = [string]::IsNullOrWhiteSpace($RecordCommit)
  if ($mode -eq 'change') {
    $snapshot = if ($useIndex) {
      Get-IndexSnapshot -Repository $Repository -BaseCommit $PriorCommit
    } else {
      Get-CommitSnapshot -Repository $Repository -BaseCommit $PriorCommit -TargetCommit $RecordCommit
    }
    if ($snapshot.Paths.Count -eq 0) { return 'V2_CHANGE_EMPTY' }
  } else {
    $recordChanges = @(
      if ($useIndex) {
        Get-IndexChangePaths -Repository $Repository -BaseCommit $PriorCommit
      } else {
        Get-CommitChangePaths -Repository $Repository -BaseCommit $PriorCommit -TargetCommit $RecordCommit
      }
    )
    if ($recordChanges.Count -ne 0) { return 'V2_SNAPSHOT_HAS_SOURCE_CHANGES' }
    $snapshotCommit = [string]$Record.source_provenance.snapshot_commit
    if (-not (Test-GitCommand -Repository $Repository -GitArgs @('cat-file', '-e', "$snapshotCommit^{commit}"))) { return 'V2_SNAPSHOT_NOT_FOUND' }
    if (-not (Test-GitAncestor -Repository $Repository -Ancestor $snapshotCommit -Descendant $PriorCommit)) { return 'V2_SNAPSHOT_UNREACHABLE' }
    $snapshotParent = Get-CommitFirstParent -Repository $Repository -Commit $snapshotCommit
    $snapshot = Get-CommitSnapshot -Repository $Repository -BaseCommit $snapshotParent -TargetCommit $snapshotCommit
    if ($snapshot.Paths.Count -eq 0) { return 'V2_SNAPSHOT_EMPTY' }
  }
  return (Compare-ProvenanceV2Snapshot -Record $Record -ExpectedPaths ([string[]]$snapshot.Paths) -ExpectedDigest ([string]$snapshot.Digest))
}

function Assert-Finding {
  param([Parameter(Mandatory)] [AllowEmptyString()] [string]$Expected, [Parameter(Mandatory)] [AllowNull()] [object]$Actual)
  if ([string]::IsNullOrEmpty($Expected)) {
    if ($null -ne $Actual) { throw "Expected clean provenance, got [$Actual]" }
    return
  }
  if ($null -eq $Actual) { throw "Expected finding [$Expected], got clean" }
  if ([string]$Actual -ne $Expected) { throw "Expected finding [$Expected], got [$Actual]" }
}

function Assert-Equal {
  param([Parameter(Mandatory)] [object]$Expected, [Parameter(Mandatory)] [object]$Actual, [Parameter(Mandatory)] [string]$Label)
  if ([string]$Expected -cne [string]$Actual) { throw "$Label expected [$Expected], got [$Actual]." }
}

function Invoke-GitIntegrationSelfTest {
  $tempRoot = [IO.Path]::GetTempPath().TrimEnd([IO.Path]::DirectorySeparatorChar)
  $testRoot = Join-Path $tempRoot ("hibiki-evidence-v2-" + [Guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($testRoot) | Out-Null
  try {
    Invoke-GitText -Repository $testRoot -GitArgs @('init', '--initial-branch=main') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('config', 'user.name', 'Hibiki Evidence SelfTest') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('config', 'user.email', 'evidence-selftest@example.invalid') | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $testRoot 'src')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $testRoot 'evidence')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot '.gitattributes'), "* text=auto`n*.txt text eol=lf`n", [Text.UTF8Encoding]::new($false))
    foreach ($name in @('a.txt', 'delete.txt', 'old.txt')) {
      [IO.File]::WriteAllText((Join-Path $testRoot "src/$name"), "$name`n", [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::WriteAllText((Join-Path $testRoot 'evidence/legacy.json'), "{}`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'initial') | Out-Null
    $base = Get-GitCommit -Repository $testRoot -Ref 'HEAD'
    Invoke-GitText -Repository $testRoot -GitArgs @('update-ref', 'refs/remotes/origin/main', $base) | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('switch', '-c', 'feature') | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot 'src/a.txt'), "alpha`r`nbeta`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $testRoot 'src/new.txt'), "old.txt`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Delete((Join-Path $testRoot 'src/old.txt'))
    [IO.File]::Delete((Join-Path $testRoot 'src/delete.txt'))
    [IO.Directory]::CreateDirectory((Join-Path $testRoot 'schemas')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot 'schemas/evidence-manifest-v2.schema.json'), "{}`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    $beforeSquash = Get-IndexSnapshot -Repository $testRoot -BaseCommit $base
    Assert-Equal -Expected 'schemas/evidence-manifest-v2.schema.json,src/a.txt,src/delete.txt,src/new.txt,src/old.txt' -Actual ($beforeSquash.Paths -join ',') -Label 'rename/delete path expansion'
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'source') | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot 'evidence/record.json'), "{}`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'evidence') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('switch', 'main') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('merge', '--squash', 'feature') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'squash') | Out-Null
    $squash = Get-GitCommit -Repository $testRoot -Ref 'HEAD'
    Invoke-GitText -Repository $testRoot -GitArgs @('update-ref', 'refs/remotes/origin/main', $squash) | Out-Null
    $introduction = Get-IntroductionCommit -Repository $testRoot -HistoryRef 'origin/main' -Path 'evidence/record.json' -AfterCommit $null
    $parent = Get-CommitFirstParent -Repository $testRoot -Commit $introduction
    $afterSquash = Get-CommitSnapshot -Repository $testRoot -BaseCommit $parent -TargetCommit $introduction
    Assert-Equal -Expected $squash -Actual $introduction -Label 'derived squash record commit'
    Assert-Equal -Expected $beforeSquash.Digest -Actual $afterSquash.Digest -Label 'pre/post squash digest'
    Assert-Equal -Expected ($beforeSquash.Paths -join ',') -Actual ($afterSquash.Paths -join ',') -Label 'pre/post squash paths'
    [IO.Directory]::CreateDirectory((Join-Path $testRoot 'notes')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot 'notes/unrelated.txt'), "later`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'unrelated') | Out-Null
    $later = Get-GitCommit -Repository $testRoot -Ref 'HEAD'
    Invoke-GitText -Repository $testRoot -GitArgs @('update-ref', 'refs/remotes/origin/main', $later) | Out-Null
    Assert-Equal -Expected $squash -Actual (Get-IntroductionCommit -Repository $testRoot -HistoryRef 'origin/main' -Path 'evidence/record.json' -AfterCommit $null) -Label 'unrelated main advance'
    Assert-Equal -Expected $squash -Actual (Get-PolicyCutoverCommit -Repository $testRoot -HistoryRef 'origin/main') -Label 'policy cutover commit'
    $baselineMap = Get-TreeBlobMap -Repository $testRoot -Commit $base
    if (-not $baselineMap.ContainsKey('evidence/legacy.json')) { throw 'Cutover baseline inventory omitted legacy evidence.' }
    $inventory = Get-EvidenceInventoryPaths -Repository $testRoot -PriorCommit $base -HistoryRef 'origin/main'
    if (-not $inventory.Contains('evidence/record.json')) { throw 'Post-cutover inventory omitted the v2 evidence path.' }
    $snapshotRecord = [pscustomobject]@{
      evidence_format = 2
      source_provenance = [pscustomobject]@{
        mode = 'snapshot'
        snapshot_commit = $squash
        paths = [string[]]$afterSquash.Paths
        digest_algorithm = $script:DigestAlgorithm
        digest = [string]$afterSquash.Digest
      }
    }
    Assert-Finding '' (Get-V2SourceSetFinding -Repository $testRoot -Record $snapshotRecord -PriorCommit $later -RecordCommit $null)
    [IO.File]::WriteAllText((Join-Path $testRoot 'evidence/record.json'), "{`"tamper`":true}`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'tamper') | Out-Null
    [IO.File]::WriteAllText((Join-Path $testRoot 'evidence/record.json'), "{}`n", [Text.UTF8Encoding]::new($false))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    Invoke-GitText -Repository $testRoot -GitArgs @('commit', '-m', 'restore-bytes') | Out-Null
    if ((Get-LatestTouchCommit -Repository $testRoot -HistoryRef 'HEAD' -Path 'evidence/record.json') -ceq $squash) {
      throw 'Latest-touch immutability check ignored a reverted evidence mutation.'
    }
    $deleteBase = Get-GitCommit -Repository $testRoot -Ref 'HEAD'
    [IO.File]::Delete((Join-Path $testRoot 'evidence/record.json'))
    Invoke-GitText -Repository $testRoot -GitArgs @('add', '--all') | Out-Null
    $deletedEvidence = @(Get-EvidenceChangePaths -Repository $testRoot -BaseCommit $deleteBase)
    $trackedAfterDelete = Get-IndexTrackedPathSet -Repository $testRoot
    if ($deletedEvidence -notcontains 'evidence/record.json' -or $trackedAfterDelete.Contains('evidence/record.json')) {
      throw 'Exact-index deletion detection did not reject a removed evidence record.'
    }
    $caseSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    [void]$caseSet.Add('evidence/case.json')
    if ($caseSet.Contains('evidence/Case.json')) { throw 'Evidence path inventory comparison became case-insensitive.' }
  } finally {
    $resolvedTemp = [IO.Path]::GetFullPath($tempRoot)
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedTest.StartsWith($resolvedTemp + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($resolvedTest).StartsWith('hibiki-evidence-v2-', [StringComparison]::Ordinal)) {
      throw "Refusing to remove unexpected self-test path '$resolvedTest'."
    }
    if (Test-Path -LiteralPath $resolvedTest) { Remove-Item -LiteralPath $resolvedTest -Recurse -Force }
  }
}

if ($SelfTest) {
  if ($DescribeCurrentChange) { throw '-SelfTest and -DescribeCurrentChange are mutually exclusive.' }
  $caseCount = 0
  $valid = [pscustomobject]@{ source_commit = 'a' * 40 }
  $history = @(('b' * 40), ('a' * 40))
  Assert-Finding '' (Get-ProvenanceFinding -Record $valid -FileHistory $history -CommitExists $true -ReachableFromMain $true); $caseCount++
  Assert-Finding MISSING_FIELD (Get-ProvenanceFinding -Record ([pscustomobject]@{}) -FileHistory @() -CommitExists $true -ReachableFromMain $true); $caseCount++
  Assert-Finding EMPTY (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = '   ' }) -FileHistory @() -CommitExists $true -ReachableFromMain $true); $caseCount++
  Assert-Finding FORMAT (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'abc123' }) -FileHistory @() -CommitExists $true -ReachableFromMain $true); $caseCount++
  Assert-Finding FORMAT (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = ('z' * 40) }) -FileHistory @() -CommitExists $true -ReachableFromMain $true); $caseCount++
  Assert-Finding NOT_FOUND (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @() -CommitExists $false -ReachableFromMain $false); $caseCount++
  Assert-Finding UNREACHABLE (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @() -CommitExists $true -ReachableFromMain $false); $caseCount++
  Assert-Finding NOT_FILE_HISTORY (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @(('c' * 40)) -CommitExists $true -ReachableFromMain $true); $caseCount++

  $v2 = [pscustomobject]@{ evidence_format = 2; source_provenance = [pscustomobject]@{ mode = 'change'; paths = @('tools/example.ps1'); digest_algorithm = $script:DigestAlgorithm; digest = ('a' * 64) } }
  Assert-Finding '' (Get-ProvenanceV2ShapeFinding -Record $v2); $caseCount++
  Assert-Finding '' (Compare-ProvenanceV2Snapshot -Record $v2 -ExpectedPaths @('tools/example.ps1') -ExpectedDigest ('a' * 64)); $caseCount++
  $withLegacyHash = $v2.PSObject.Copy(); $withLegacyHash | Add-Member source_commit ('b' * 40)
  Assert-Finding V2_SOURCE_COMMIT_FORBIDDEN (Get-ProvenanceV2ShapeFinding -Record $withLegacyHash); $caseCount++
  $wrongDigest = $v2.PSObject.Copy(); $wrongDigest.source_provenance = $v2.source_provenance.PSObject.Copy(); $wrongDigest.source_provenance.digest = ('b' * 64)
  Assert-Finding V2_DIGEST_MISMATCH (Compare-ProvenanceV2Snapshot -Record $wrongDigest -ExpectedPaths @('tools/example.ps1') -ExpectedDigest ('a' * 64)); $caseCount++
  $wrongPaths = $v2.PSObject.Copy(); $wrongPaths.source_provenance = $v2.source_provenance.PSObject.Copy(); $wrongPaths.source_provenance.paths = @('tools/other.ps1')
  Assert-Finding V2_PATH_SET (Compare-ProvenanceV2Snapshot -Record $wrongPaths -ExpectedPaths @('tools/example.ps1') -ExpectedDigest ('a' * 64)); $caseCount++
  $snapshotV2 = [pscustomobject]@{ evidence_format = 2; source_provenance = [pscustomobject]@{ mode = 'snapshot'; snapshot_commit = ('b' * 40); paths = @('tools/example.ps1'); digest_algorithm = $script:DigestAlgorithm; digest = ('a' * 64) } }
  Assert-Finding '' (Get-ProvenanceV2ShapeFinding -Record $snapshotV2); $caseCount++
  $changeWithSnapshot = $v2.PSObject.Copy(); $changeWithSnapshot.source_provenance = $v2.source_provenance.PSObject.Copy(); $changeWithSnapshot.source_provenance | Add-Member snapshot_commit ('b' * 40)
  Assert-Finding V2_CHANGE_SNAPSHOT_COMMIT_FORBIDDEN (Get-ProvenanceV2ShapeFinding -Record $changeWithSnapshot); $caseCount++

  foreach ($pathCase in @(
    @{ Expected = 'V2_PATHS_EMPTY'; Paths = @() },
    @{ Expected = 'V2_PATH_TRAVERSAL'; Paths = @('../x') },
    @{ Expected = 'V2_PATH_FORMAT'; Paths = @('tools\x.ps1') },
    @{ Expected = 'V2_PATH_FORMAT'; Paths = @('tools/*.ps1') },
    @{ Expected = 'V2_PATH_FORMAT'; Paths = @("tools/x`n.ps1") },
    @{ Expected = 'V2_PATH_EVIDENCE'; Paths = @('Evidence/x.json') },
    @{ Expected = 'V2_PATH_GIT_INTERNAL'; Paths = @('.Git/config') },
    @{ Expected = 'V2_PATH_DUPLICATE'; Paths = @('A.txt', 'a.txt') },
    @{ Expected = 'V2_PATHS_UNSORTED'; Paths = @('z.txt', 'a.txt') }
  )) {
    Assert-Finding $pathCase.Expected (Get-PathListFinding -Paths ([string[]]$pathCase.Paths)); $caseCount++
  }

  $absent = [pscustomobject]@{ State = 'D'; Mode = ''; Size = 0L; Sha256 = '' }
  $blob3 = [pscustomobject]@{ State = 'F'; Mode = '100644'; Size = 3L; Sha256 = ('c' * 64) }
  $entries = @(
    [pscustomobject]@{ Path = 'b.txt'; Before = $blob3; After = $absent },
    [pscustomobject]@{ Path = 'a.txt'; Before = $absent; After = $blob3 }
  )
  $snapshotA = New-CanonicalSnapshot -Entries $entries
  $snapshotB = New-CanonicalSnapshot -Entries @($entries[1], $entries[0])
  Assert-Equal '35f405ec79eaf610985ae2f6750753ecf598ed3b4ac1ad3cae17e47cbf7123e1' $snapshotA.Digest 'canonical fixed vector'; $caseCount++
  Assert-Equal $snapshotA.Digest $snapshotB.Digest 'canonical entry ordering'; $caseCount++
  $blob4 = [pscustomobject]@{ State = 'F'; Mode = '100644'; Size = 4L; Sha256 = ('c' * 64) }
  $snapshotC = New-CanonicalSnapshot -Entries @([pscustomobject]@{ Path = 'a.txt'; Before = $absent; After = $blob4 }, $entries[0])
  if ($snapshotC.Digest -ceq $snapshotA.Digest) { throw 'Canonical digest did not bind blob size.' }; $caseCount++
  Assert-Finding '$.a' (Get-DuplicateJsonProperty -Json '{"a":1,"a":2}'); $caseCount++

  $validSchemaRecord = [ordered]@{
    evidence_format = 2
    issue = 1
    source_provenance = [ordered]@{ mode = 'change'; paths = @('tools/example.ps1'); digest_algorithm = $script:DigestAlgorithm; digest = ('a' * 64) }
    metadata = [ordered]@{
      scope = 'selftest-v2'; environment_fingerprint = 'windows-amd64-pwsh-selftest'
      commands = @('pwsh -File tools/example.ps1')
      results = @([ordered]@{ command = 'example'; status = 'pass'; detail = 'offline self-test' })
      limitations = @('Source binding only; no live device claim.')
      logs = [ordered]@{}; tool_versions = [ordered]@{ powershell = '7.x' }
      started_at = '2026-08-24T00:00:00+08:00'; finished_at = '2026-08-24T00:01:00+08:00'
    }
  }
  $validSchemaJson = $validSchemaRecord | ConvertTo-Json -Depth 8
  if (-not (Test-V2SchemaText -Json $validSchemaJson)) { throw 'Valid evidence v2 schema record was rejected.' }; $caseCount++
  foreach ($invalidCase in @(
    @{ Label = 'source_commit'; Mutate = { param($case) $case | Add-Member source_commit ('a' * 40) } },
    @{ Label = 'schema_version'; Mutate = { param($case) $case | Add-Member schema_version 2 } },
    @{ Label = 'snapshot-without-commit'; Mutate = { param($case) $case.source_provenance.mode = 'snapshot' } },
    @{ Label = 'evidence-source-path'; Mutate = { param($case) $case.source_provenance.paths = @('Evidence/a.json') } },
    @{ Label = 'digest-final-lf'; Mutate = { param($case) $case.source_provenance.digest = (('a' * 64) + "`n") } },
    @{ Label = 'supersedes-traversal'; Mutate = { param($case) $case | Add-Member supersedes @('evidence/../x.json') } },
    @{ Label = 'result-control'; Mutate = { param($case) $case.metadata.results[0].detail = "bad$([char]27)value" } },
    @{ Label = 'invalid-date'; Mutate = { param($case) $case.metadata.started_at = '2026-99-99T00:00:00+08:00' } },
    @{ Label = 'unknown-result-field'; Mutate = { param($case) $case.metadata.results[0] | Add-Member extra 'x' } }
  )) {
    $case = $validSchemaJson | ConvertFrom-Json
    & $invalidCase.Mutate $case
    if (Test-V2SchemaText -Json ($case | ConvertTo-Json -Depth 8)) { throw "Evidence v2 schema accepted invalid case '$($invalidCase.Label)'." }
    $caseCount++
  }
  $badTime = $validSchemaJson | ConvertFrom-Json
  $badTime.metadata.started_at = '2026-02-31T00:00:00+08:00'
  Assert-Finding V2_TIME_FORMAT (Get-V2TimeFinding -Record $badTime); $caseCount++
  $reverseTime = $validSchemaJson | ConvertFrom-Json
  $reverseTime.metadata.finished_at = '2026-08-23T00:00:00+08:00'
  Assert-Finding V2_TIME_ORDER (Get-V2TimeFinding -Record $reverseTime); $caseCount++
  $successorMap = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
  $firstSuccessor = [pscustomobject]@{ supersedes = @('evidence/old.json') }
  Assert-Finding '' (Get-DuplicateSuccessorFinding -Successors $successorMap -Record $firstSuccessor -RecordPath 'evidence/new-a.json'); $caseCount++
  Assert-Finding 'target=evidence/old.json other=evidence/new-a.json' (Get-DuplicateSuccessorFinding -Successors $successorMap -Record $firstSuccessor -RecordPath 'evidence/new-b.json'); $caseCount++
  foreach ($deletionCase in @(
    @{ Label = 'existing-present'; Base = $true; Index = $true; Worktree = $true; Expected = $false },
    @{ Label = 'existing-unstaged-delete'; Base = $true; Index = $true; Worktree = $false; Expected = $true },
    @{ Label = 'existing-staged-delete'; Base = $true; Index = $false; Worktree = $false; Expected = $true },
    @{ Label = 'new-untracked-add'; Base = $false; Index = $false; Worktree = $true; Expected = $false },
    @{ Label = 'new-staged-then-deleted'; Base = $false; Index = $true; Worktree = $false; Expected = $true }
  )) {
    $actualDeletion = Test-EvidenceDeletionState -ExistedAtBase $deletionCase.Base -TrackedInIndex $deletionCase.Index -ExistsInWorktree $deletionCase.Worktree
    Assert-Equal -Expected $deletionCase.Expected -Actual $actualDeletion -Label $deletionCase.Label
    $caseCount++
  }
  Invoke-GitIntegrationSelfTest; $caseCount++
  Write-Output "evidence-audit self-tests passed ($caseCount cases)."
  exit 0
}

$resolvedKind = if ($CandidateKind -eq 'auto' -and -not [string]::IsNullOrWhiteSpace($env:GITHUB_EVENT_NAME)) { $env:GITHUB_EVENT_NAME } else { $CandidateKind }
if ($resolvedKind -eq 'auto') { $resolvedKind = 'workflow_dispatch' }
$originMain = Get-GitCommit -Repository $repo -Ref 'origin/main'
$headCommit = Get-GitCommit -Repository $repo -Ref 'HEAD'
$mainRef = if ($resolvedKind -eq 'push' -and $env:GITHUB_REF_NAME -eq 'main') { $headCommit } else { $originMain }
if ($resolvedKind -eq 'push' -and $env:GITHUB_REF_NAME -eq 'main') {
  $candidateBase = Get-CommitFirstParent -Repository $repo -Commit $headCommit
} else {
  $mergeBaseLines = @(Invoke-GitText -Repository $repo -GitArgs @('merge-base', $mainRef, $headCommit))
  if ($mergeBaseLines.Count -ne 1 -or $mergeBaseLines[0] -notmatch '^[0-9a-f]{40,64}$') { throw 'Unable to derive the candidate merge base.' }
  $candidateBase = $mergeBaseLines[0]
}

if ($DescribeCurrentChange) {
  $snapshot = Get-IndexSnapshot -Repository $repo -BaseCommit $candidateBase
  if ($snapshot.Paths.Count -eq 0) { throw 'The staged candidate has no non-evidence source changes to describe.' }
  [ordered]@{ mode = 'change'; paths = [string[]]$snapshot.Paths; digest_algorithm = $script:DigestAlgorithm; digest = [string]$snapshot.Digest } | ConvertTo-Json -Depth 5
  exit 0
}

$findings = [Collections.Generic.List[string]]::new()
$bindings = [Collections.Generic.List[string]]::new()
$files = @(Get-ChildItem -LiteralPath (Join-Path $repo 'evidence') -Recurse -Filter '*.json' -File | Sort-Object FullName)
$changedEvidence = Get-EvidenceChangePaths -Repository $repo -BaseCommit $candidateBase
$changedSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$successors = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($changedPath in $changedEvidence) { [void]$changedSet.Add($changedPath) }
$indexPaths = Get-IndexTrackedPathSet -Repository $repo
$worktreePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($file in $files) {
  [void]$worktreePaths.Add(([IO.Path]::GetRelativePath($repo, $file.FullName) -replace '\\', '/'))
}
$baseEvidenceTree = Get-TreeBlobMap -Repository $repo -Commit $candidateBase
foreach ($changedPath in $changedEvidence) {
  if (Test-EvidenceDeletionState -ExistedAtBase ($baseEvidenceTree.ContainsKey($changedPath)) -TrackedInIndex ($indexPaths.Contains($changedPath)) -ExistsInWorktree ($worktreePaths.Contains($changedPath))) {
    $findings.Add("EVIDENCE_DELETE_FORBIDDEN|$changedPath|")
  }
}

$cutoverCommit = Get-PolicyCutoverCommit -Repository $repo -HistoryRef $mainRef
$legacyBaseline = $null
if (-not [string]::IsNullOrWhiteSpace($cutoverCommit)) {
  $cutoverPrior = Get-CommitFirstParent -Repository $repo -Commit $cutoverCommit
  $legacyBaseline = Get-TreeBlobMap -Repository $repo -Commit $cutoverPrior
  $currentEvidenceTree = Get-TreeBlobMap -Repository $repo -Commit $mainRef
  foreach ($entry in $legacyBaseline.GetEnumerator()) {
    if ($entry.Key -notmatch '(?i)\.json$') { continue }
    if (-not $currentEvidenceTree.ContainsKey($entry.Key) -or
        $currentEvidenceTree[$entry.Key].Mode -cne $entry.Value.Mode -or
        $currentEvidenceTree[$entry.Key].ObjectId -cne $entry.Value.ObjectId) {
      $findings.Add("LEGACY_BASELINE_MUTATED_OR_DELETED|$($entry.Key)|cutover=$cutoverCommit")
    }
  }
  $inventoryPaths = Get-EvidenceInventoryPaths -Repository $repo -PriorCommit $cutoverPrior -HistoryRef $mainRef
  foreach ($inventoryPath in $inventoryPaths) {
    if (-not $currentEvidenceTree.ContainsKey($inventoryPath)) {
      $findings.Add("EVIDENCE_INVENTORY_MISSING|$inventoryPath|cutover=$cutoverCommit")
    }
  }
}

$v2Count = 0
foreach ($file in $files) {
  $relative = [IO.Path]::GetRelativePath($repo, $file.FullName) -replace '\\', '/'
  try {
    if ($file.Length -gt 1048576L) { throw 'Evidence JSON exceeds the 1 MiB audit limit.' }
    $raw = Get-Content -LiteralPath $file.FullName -Raw
    $record = $raw | ConvertFrom-Json
  } catch {
    $findings.Add("PARSE|$relative|$($_.Exception.Message)")
    continue
  }
  $isV2 = ($null -ne $record.PSObject.Properties['evidence_format'] -or $null -ne $record.PSObject.Properties['source_provenance'])
  $isCandidateChange = $changedSet.Contains($relative)
  if (-not $isV2) {
    if ($isCandidateChange) { $findings.Add("LEGACY_WRITE_FORBIDDEN|$relative|use evidence_format 2 in a new file"); continue }
    if ($null -ne $legacyBaseline -and -not $legacyBaseline.ContainsKey($relative)) {
      $findings.Add("LEGACY_AFTER_CUTOVER|$relative|use evidence_format 2"); continue
    }
    $history = [string[]]@(Invoke-GitText -Repository $repo -GitArgs @('rev-list', $mainRef, '--', $relative))
    $hash = if ($null -ne $record.PSObject.Properties['source_commit']) { [string]$record.source_commit } else { '' }
    $exists = $false; $reachable = $false
    if ($hash -match '^[0-9a-f]{40}$') {
      $exists = Test-GitCommand -Repository $repo -GitArgs @('cat-file', '-e', "$hash^{commit}")
      if ($exists) { $reachable = Test-GitAncestor -Repository $repo -Ancestor $hash -Descendant $mainRef }
    }
    $finding = Get-ProvenanceFinding -Record $record -FileHistory $history -CommitExists:$exists -ReachableFromMain:$reachable
    if ($finding) { $findings.Add("$finding|$relative|$hash") }
    continue
  }

  $v2Count++
  try {
    $duplicate = Get-DuplicateJsonProperty -Json $raw
    if ($duplicate) { $findings.Add("V2_DUPLICATE_JSON_KEY|$relative|$duplicate"); continue }
    if (-not (Test-V2SchemaText -Json $raw)) { $findings.Add("V2_SCHEMA|$relative|"); continue }
    $shapeFinding = Get-ProvenanceV2ShapeFinding -Record $record
    if ($shapeFinding) { $findings.Add("$shapeFinding|$relative|"); continue }
    $timeFinding = Get-V2TimeFinding -Record $record
    if ($timeFinding) { $findings.Add("$timeFinding|$relative|"); continue }
    $duplicateSuccessor = Get-DuplicateSuccessorFinding -Successors $successors -Record $record -RecordPath $relative
    if ($duplicateSuccessor) { $findings.Add("V2_SUPERSEDES_DUPLICATE_SUCCESSOR|$relative|$duplicateSuccessor"); continue }

    if ($isCandidateChange) {
      if (Test-PathAtCommit -Repository $repo -Commit $candidateBase -Path $relative) { $findings.Add("V2_APPEND_ONLY|$relative|"); continue }
      if ($resolvedKind -eq 'merge_group') {
        $recordCommit = Get-IntroductionCommit -Repository $repo -HistoryRef $headCommit -Path $relative -AfterCommit $candidateBase
        $priorCommit = Get-CommitFirstParent -Repository $repo -Commit $recordCommit
        $sourceFinding = Get-V2SourceSetFinding -Repository $repo -Record $record -PriorCommit $priorCommit -RecordCommit $recordCommit
        $binding = $recordCommit
      } else {
        $priorCommit = $candidateBase
        $sourceFinding = Get-V2SourceSetFinding -Repository $repo -Record $record -PriorCommit $priorCommit -RecordCommit $null
        $binding = 'candidate-index'
      }
    } else {
      $recordCommit = Get-IntroductionCommit -Repository $repo -HistoryRef $mainRef -Path $relative -AfterCommit $null
      $latestTouch = Get-LatestTouchCommit -Repository $repo -HistoryRef $mainRef -Path $relative
      if ($latestTouch -cne $recordCommit) {
        $findings.Add("V2_MUTATED|$relative|introduced=$recordCommit latest=$latestTouch"); continue
      }
      $priorCommit = Get-CommitFirstParent -Repository $repo -Commit $recordCommit
      $introducedEntry = Get-GitTreeEntry -Repository $repo -Commit $recordCommit -Path $relative
      $currentEntry = Get-GitTreeEntry -Repository $repo -Commit $mainRef -Path $relative
      if ($introducedEntry.ObjectId -cne $currentEntry.ObjectId -or $introducedEntry.Mode -cne $currentEntry.Mode) {
        $findings.Add("V2_MUTATED|$relative|introduced=$recordCommit"); continue
      }
      $sourceFinding = Get-V2SourceSetFinding -Repository $repo -Record $record -PriorCommit $priorCommit -RecordCommit $recordCommit
      $binding = $recordCommit
    }
    if ($sourceFinding) { $findings.Add("$sourceFinding|$relative|$binding"); continue }
    $supersedesFinding = Test-SupersedesTargets -Repository $repo -Record $record -RecordPath $relative -PriorCommit $priorCommit
    if ($supersedesFinding) { $findings.Add("$supersedesFinding|$relative|$binding"); continue }
    $bindings.Add("BOUND|$relative|$binding")
  } catch {
    $findings.Add("V2_AUDIT_ERROR|$relative|$($_.Exception.Message)")
  }
}

Write-Output ("checked=" + $files.Count)
Write-Output ("v2_checked=" + $v2Count)
Write-Output ("findings=" + $findings.Count)
$bindings | ForEach-Object { Write-Output $_ }
$findings | ForEach-Object { Write-Output $_ }
if ($findings.Count -gt 0) { exit 1 }
exit 0
