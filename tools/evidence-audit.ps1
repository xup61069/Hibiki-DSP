#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-ProvenanceFinding {
  param(
    [Parameter(Mandatory)] [psobject]$Record,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$FileHistory,
    [Parameter(Mandatory)] [bool]$CommitExists,
    [Parameter(Mandatory)] [bool]$ReachableFromMain
  )

  $hasSourceCommit = $false
  if ($null -ne $Record.PSObject.Properties['source_commit']) { $hasSourceCommit = $true }
  if (-not $hasSourceCommit) {
    return 'MISSING_FIELD'
  }

  $hash = [string]$Record.source_commit
  if ([string]::IsNullOrWhiteSpace($hash)) {
    return 'EMPTY'
  }
  if ($hash -notmatch '^[0-9a-f]{40}$') {
    return 'FORMAT'
  }
  if (-not $CommitExists) {
    return 'NOT_FOUND'
  }
  if (-not $ReachableFromMain) {
    return 'UNREACHABLE'
  }
  if (@($FileHistory) -notcontains $hash) {
    return 'NOT_FILE_HISTORY'
  }
  return $null
}

function Assert-Finding {
  param(
    [Parameter(Mandatory)] [AllowEmptyString()] [string]$Expected,
    [Parameter(Mandatory)] [AllowNull()] [object]$Actual
  )
  if ([string]::IsNullOrEmpty($Expected)) {
    if ($null -ne $Actual) { throw "Expected clean provenance, got [$Actual]" }
    return
  }
  if ($null -eq $Actual) { throw "Expected finding [$Expected], got clean" }
  if ([string]$Actual -ne $Expected) { throw "Expected finding [$Expected], got [$Actual]" }
}

if ($SelfTest) {
  $valid = [pscustomobject]@{ source_commit = 'a' * 40 }
  $history = @(('b' * 40), ('a' * 40))
  Assert-Finding -Expected '' -Actual (Get-ProvenanceFinding -Record $valid -FileHistory $history -CommitExists $true -ReachableFromMain $true)

  Assert-Finding MISSING_FIELD (Get-ProvenanceFinding -Record ([pscustomobject]@{}) -FileHistory @() -CommitExists $true -ReachableFromMain $true)
  Assert-Finding EMPTY (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = '   ' }) -FileHistory @() -CommitExists $true -ReachableFromMain $true)
  Assert-Finding FORMAT (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'abc123' }) -FileHistory @() -CommitExists $true -ReachableFromMain $true)
  $nonHexHash = ('z' * 40)
  Assert-Finding FORMAT (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = $nonHexHash }) -FileHistory @() -CommitExists $true -ReachableFromMain $true)
  Assert-Finding NOT_FOUND (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @() -CommitExists $false -ReachableFromMain $false)
  Assert-Finding UNREACHABLE (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @() -CommitExists $true -ReachableFromMain $false)
  Assert-Finding NOT_FILE_HISTORY (Get-ProvenanceFinding -Record ([pscustomobject]@{ source_commit = 'a' * 40 }) -FileHistory @(('c' * 40)) -CommitExists $true -ReachableFromMain $true)

  Write-Output 'evidence-audit self-tests passed.'
  exit 0
}

$mainSha = (& git -C $repo rev-parse --verify 'origin/main^{commit}').Trim()
if ([string]::IsNullOrWhiteSpace($mainSha)) { throw 'origin/main is not available locally; fetch before auditing evidence.' }

$findings = [System.Collections.Generic.List[string]]::new()
$files = @(Get-ChildItem -LiteralPath (Join-Path $repo 'evidence') -Recurse -Filter '*.json' -File | Sort-Object FullName)
foreach ($file in $files) {
  $relative = [IO.Path]::GetRelativePath($repo, $file.FullName) -replace '\\', '/'
  $label = "$relative"
  try {
    $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
  } catch {
    $findings.Add("PARSE|$label|$($_.Exception.Message)")
    continue
  }

  $history = @(& git -C $repo rev-list origin/main -- $relative 2>$null | ForEach-Object { $_.Trim() })
  $hash = ''
  if ($null -ne $record.PSObject.Properties['source_commit']) { $hash = [string]$record.source_commit }
  $exists = $false
  $reachable = $false
  if ($hash -match '^[0-9a-f]{40}$') {
    & git -C $repo cat-file -e "$hash^{commit}" 2>$null
    $exists = ($LASTEXITCODE -eq 0)
    if ($exists) {
      & git -C $repo merge-base --is-ancestor $hash origin/main 2>$null
      $reachable = ($LASTEXITCODE -eq 0)
    }
  }

  $finding = Get-ProvenanceFinding -Record $record -FileHistory ([string[]]$history) -CommitExists:$exists -ReachableFromMain:$reachable
  if ($finding) { $findings.Add("$finding|$label|$hash") }
}

Write-Output ("checked=" + $files.Count)
Write-Output ("findings=" + $findings.Count)
$findings | ForEach-Object { Write-Output $_ }
if ($findings.Count -gt 0) { exit 1 }
exit 0
