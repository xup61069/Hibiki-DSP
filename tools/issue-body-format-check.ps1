#Requires -Version 7
[CmdletBinding()]
param(
  [int]$Issue = 0,
  [string]$BodyFile,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Normalize-Newlines {
  param([Parameter(Mandatory)] [string]$Body)

  $normalized = $Body.Replace([string][char]13 + [string][char]10, [string][char]10)
  return $normalized.Replace([string][char]13, [string][char]10)
}

function New-FormatReport {
  param(
    [Parameter(Mandatory)] [string]$Status,
    [Parameter(Mandatory)] [string]$Message,
    [Parameter(Mandatory)] [int]$LineCount
  )

  return [pscustomobject]@{
    Status = $Status
    Message = $Message
    LineCount = $LineCount
  }
}

function Get-HandoffFormatReport {
  param([Parameter(Mandatory)] [string]$Body)

  $normalized = Normalize-Newlines -Body $Body
  $lineCount = @($normalized -split [string][char]10).Count
  $validBlock = [regex]::Match(
    $normalized,
    '(?ms)^[ \t]*<!-- hibiki:handoff-v1[ \t]*\n(?<body>.*?)\n[ \t]*-->[ \t]*$'
  )
  if ($validBlock.Success -and
      -not [string]::IsNullOrWhiteSpace($validBlock.Groups['body'].Value)) {
    return New-FormatReport -Status 'Valid' -LineCount $lineCount -Message 'Handoff block is line-oriented and has a closing marker.'
  }

  $singleLineBlock = [regex]::IsMatch(
    $normalized.TrimEnd(),
    '(?m)^[ \t]*<!-- hibiki:handoff-v1[^\n]*-->[ \t]*$'
  )
  if ($singleLineBlock) {
    return New-FormatReport `
      -Status 'SingleLineCompressed' `
      -LineCount $lineCount `
      -Message 'Handoff block exists but is compressed onto one line. Rebuild the body from a line array and use gh issue edit --body-file <path> so the opening marker, each field, and --> are separate lines.'
  }

  $hasMarkerText = $normalized.Contains('hibiki:handoff-v1')
  $hasClosingMarker = $normalized.Contains('-->')
  if ($hasMarkerText -or $hasClosingMarker) {
    return New-FormatReport `
      -Status 'Malformed' `
      -LineCount $lineCount `
      -Message 'Handoff marker text is present, but the block is not in the required line-oriented form. Rebuild the body from a line array and use gh issue edit --body-file <path>.'
  }

  return New-FormatReport `
    -Status 'Missing' `
    -LineCount $lineCount `
    -Message 'Issue body is missing the hibiki:handoff-v1 block.'
}

function Get-IssueBody {
  param([Parameter(Mandatory)] [int]$IssueNumber)

  $json = & gh issue view $IssueNumber --json number,body 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "Issue #$IssueNumber could not be read via gh: $json"
  }
  $issue = $json | ConvertFrom-Json
  return [string]$issue.body
}

function Assert-Report {
  param(
    [Parameter(Mandatory)] $Report,
    [Parameter(Mandatory)] [string]$ExpectedStatus,
    [Parameter(Mandatory)] [string]$Label
  )

  if ($Report.Status -ne $ExpectedStatus) {
    throw "issue-body-format-check self-test failed: $Label expected '$ExpectedStatus', got '$($Report.Status)'."
  }
}

if ($SelfTest) {
  if ($Issue -ne 0 -or -not [string]::IsNullOrWhiteSpace($BodyFile)) {
    throw 'SelfTest cannot be combined with -Issue or -BodyFile.'
  }

  $normalLines = @(
    '# Objective',
    '',
    '<!-- hibiki:handoff-v1',
    'schema_version: 2',
    'issue: 99',
    'branch: codex/99-format-selftest',
    'scope_globs: ["tools/example.ps1"]',
    '-->'
  )
  $normal = $normalLines -join [Environment]::NewLine
  $report = Get-HandoffFormatReport -Body $normal
  Assert-Report -Report $report -ExpectedStatus 'Valid' -Label 'normal LF body'

  $crlf = $normal.Replace([string][char]10, [string][char]13 + [string][char]10)
  $report = Get-HandoffFormatReport -Body $crlf
  Assert-Report -Report $report -ExpectedStatus 'Valid' -Label 'normal CRLF body'

  $compressed = @(
    '# Objective',
    '<!-- hibiki:handoff-v1 schema_version: 2 issue: 99 branch: codex/99-format-selftest -->'
  ) -join [Environment]::NewLine
  $report = Get-HandoffFormatReport -Body $compressed
  Assert-Report -Report $report -ExpectedStatus 'SingleLineCompressed' -Label 'compressed handoff body'
  if ($report.Message -notmatch 'compressed onto one line' -or
      $report.Message -notmatch 'gh issue edit --body-file') {
    throw 'issue-body-format-check self-test failed: compressed-body guidance is incomplete.'
  }

  $proseOnly = 'The body mentions <!-- hibiki:handoff-v1 --> but has no block.'
  $report = Get-HandoffFormatReport -Body $proseOnly
  Assert-Report -Report $report -ExpectedStatus 'Malformed' -Label 'marker mention without block'

  $missing = '# Objective`n`nNo handoff block here.'
  $report = Get-HandoffFormatReport -Body $missing
  Assert-Report -Report $report -ExpectedStatus 'Missing' -Label 'missing handoff block'

  Write-Output 'issue-body-format-check self-test passed (normal LF/CRLF, compressed, malformed, missing: 5 cases).'
  exit 0
}

if ($Issue -gt 0 -and -not [string]::IsNullOrWhiteSpace($BodyFile)) {
  throw 'Specify either -Issue or -BodyFile, not both.'
}
if ($Issue -le 0 -and [string]::IsNullOrWhiteSpace($BodyFile)) {
  throw 'Specify a positive -Issue, a -BodyFile, or -SelfTest.'
}

if (-not [string]::IsNullOrWhiteSpace($BodyFile)) {
  if (-not (Test-Path -LiteralPath $BodyFile -PathType Leaf)) {
    throw "Body file was not found: $BodyFile"
  }
  $body = Get-Content -LiteralPath $BodyFile -Raw
  $source = $BodyFile
} else {
  $body = Get-IssueBody -IssueNumber $Issue
  $source = "Issue #$Issue"
}

$result = Get-HandoffFormatReport -Body $body
if ($result.Status -ne 'Valid') {
  throw "${source}: $($result.Message)"
}

Write-Output "$source handoff body format is valid ($($result.LineCount) lines)."
