[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$required = @(
  'AGENTS.md', 'README.md', 'docs/START_HERE.md', 'docs/AI_HANDOFF.md',
  'docs/PROJECT_MAP.md', 'docs/state/BASELINE.md', 'docs/tasks/active/0.md',
  'docs/specs/INDEX.md', 'docs/ai/HANDOFF_SCHEMA.json'
)

foreach ($relative in $required) {
  if (-not (Test-Path -LiteralPath (Join-Path $repo $relative))) {
    throw "Handoff entry is missing: $relative"
  }
}

$agents = Get-Content -LiteralPath (Join-Path $repo 'AGENTS.md') -Raw
$start = Get-Content -LiteralPath (Join-Path $repo 'docs/START_HERE.md') -Raw
$summary = Get-Content -LiteralPath (Join-Path $repo 'docs/AI_HANDOFF.md') -Raw
foreach ($check in @(
  @{ Text = $agents; Token = 'docs/AI_HANDOFF.md'; File = 'AGENTS.md' },
  @{ Text = $start; Token = 'docs/AI_HANDOFF.md'; File = 'docs/START_HERE.md' },
  @{ Text = $summary; Token = '唯一下一步'; File = 'docs/AI_HANDOFF.md' },
  @{ Text = $summary; Token = '不可自行做的事'; File = 'docs/AI_HANDOFF.md' },
  @{ Text = $summary; Token = 'tools/context-pack.ps1'; File = 'docs/AI_HANDOFF.md' }
)) {
  if (-not $check.Text.Contains($check.Token)) {
    throw "Handoff anchor missing in $($check.File): $($check.Token)"
  }
}

$handoffPath = Join-Path $repo 'docs/tasks/active/0.md'
$handoff = Get-Content -LiteralPath $handoffPath -Raw
if (-not $handoff.StartsWith("---")) { throw 'Active handoff must start with YAML front matter.' }
$frontMatter = [regex]::Match($handoff, '(?s)^---\s*(?<body>.*?)\s*---').Groups['body'].Value
if ([string]::IsNullOrWhiteSpace($frontMatter)) { throw 'Active handoff front matter is missing.' }
foreach ($field in @('schema_version', 'issue', 'branch', 'base_commit', 'status', 'updated_at')) {
  if ($frontMatter -notmatch "(?m)^${field}:\s*.+$") {
    throw "Active handoff front matter is missing: $field"
  }
}
$base = [regex]::Match($frontMatter, '(?m)^base_commit:\s*(?<value>[0-9a-fA-F]{7,40})\s*$').Groups['value'].Value
if ([string]::IsNullOrWhiteSpace($base)) { throw 'Active handoff base_commit must be a Git SHA.' }
& git -C $repo cat-file -e "$base^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Active handoff base_commit is not present locally: $base" }
& git -C $repo merge-base --is-ancestor $base HEAD
if ($LASTEXITCODE -ne 0) { throw "Active handoff base_commit is not an ancestor of HEAD: $base" }
foreach ($heading in @('## Completed', '## Last verification', '## Remaining work', '## Next safe action', '## Resume commands')) {
  if (-not $handoff.Contains($heading)) { throw "Active handoff is missing heading: $heading" }
}

Write-Output 'AI handoff checks passed (canonical entry, active state and Git ancestry).'
