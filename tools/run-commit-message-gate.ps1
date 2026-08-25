#Requires -Version 7
# Checks every commit in a pull-request range for known message corruption.
# This wrapper exists because GitHub checkouts may not contain merge bases or
# named refs for event SHAs, so the range must be resolved explicitly.
[CmdletBinding()]
param(
  [string]$BaseSha,
  [string]$HeadSha
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('BaseSha') -or [string]::IsNullOrWhiteSpace($BaseSha)) {
  $BaseSha = $env:BASE_SHA
}
if (-not $PSBoundParameters.ContainsKey('HeadSha') -or [string]::IsNullOrWhiteSpace($HeadSha)) {
  $HeadSha = $env:HEAD_SHA
}
if ([string]::IsNullOrWhiteSpace($BaseSha)) { throw 'BASE_SHA or -BaseSha is required.' }
if ([string]::IsNullOrWhiteSpace($HeadSha)) { throw 'HEAD_SHA or -HeadSha is required.' }

foreach ($sha in @($BaseSha, $HeadSha)) {
  if ($sha -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Commit SHA must be a full 40-character Git SHA: $sha"
  }
  & git cat-file -e "$sha^{commit}" 2>$null
  if ($LASTEXITCODE -ne 0) { throw "Commit is unavailable in this checkout: $sha" }
}

$mergeBase = (& git merge-base $BaseSha $HeadSha | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($mergeBase)) {
  throw "Cannot resolve a merge base for $BaseSha and $HeadSha."
}

& git rev-list --reverse "$mergeBase..$HeadSha"
if ($LASTEXITCODE -ne 0) { throw "Cannot enumerate commits from $mergeBase to $HeadSha." }
$commits = @(& git rev-list --reverse "$mergeBase..$HeadSha" | ForEach-Object { $_ })
if ($commits.Count -lt 1) {
  throw "No pull-request commits found between merge base $mergeBase and head $HeadSha."
}

$gate = Join-Path -Path $PSScriptRoot -ChildPath 'check-commit-message.ps1'
foreach ($commit in $commits) {
  $rawMessage = @(& git show -s -s --format=%B $commit)
  $message = (($rawMessage | ForEach-Object { [string]$_ }) -join [string][char]10) + [string][char]10
  $messagePath = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ("hibiki-commit-" + [Guid]::NewGuid().ToString('N') + ".txt")
  try {
    [IO.File]::WriteAllText($messagePath, $message, [Text.UTF8Encoding]::new($false))
    & $gate -MessageFile $messagePath
    if ($LASTEXITCODE -ne 0) {
      throw "Commit message integrity gate failed for $commit."
    }
    Write-Output "Checked $commit"
  }
  finally {
    Remove-Item -LiteralPath $messagePath -Force -ErrorAction SilentlyContinue
  }
}
