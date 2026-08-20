[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][int]$Issue,
  [switch]$NoSource
)

$repo = Split-Path -Parent $PSScriptRoot
$handoff = Join-Path $repo "docs/tasks/active/$Issue.md"
if (-not (Test-Path $handoff)) {
  throw "No handoff exists for Issue $Issue. Create docs/tasks/active/$Issue.md before editing."
}

$handoffText = Get-Content -LiteralPath $handoff -Raw
$specIds = @([regex]::Matches($handoffText, 'SPEC-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$adrIds = @([regex]::Matches($handoffText, 'ADR-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)

function Write-ContextFile([string]$label, [string]$path) {
  if (-not (Test-Path $path)) { throw "Context file missing: $path" }
  Write-Output "=== $label :: $path ==="
  Get-Content -LiteralPath $path
}

Write-Output "=== Hibiki context pack: Issue #$Issue ==="
Write-ContextFile 'RULES' (Join-Path $repo 'AGENTS.md')
Write-ContextFile 'START' (Join-Path $repo 'docs/START_HERE.md')
Write-ContextFile 'MAP' (Join-Path $repo 'docs/PROJECT_MAP.md')
Write-ContextFile 'BASELINE' (Join-Path $repo 'docs/state/BASELINE.md')
Write-Output '=== HANDOFF ==='
Get-Content -LiteralPath $handoff

foreach ($id in $specIds) {
  $file = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/specs') -Filter "$id-*.md" -File | Select-Object -First 1
  if ($file) { Write-ContextFile $id $file.FullName }
}
foreach ($id in $adrIds) {
  $file = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/adr') -Filter "*-*.md" -File |
    Where-Object { Select-String -LiteralPath $_.FullName -Pattern "ADR-$($id.Substring(4))" -Quiet } |
    Select-Object -First 1
  if ($file) { Write-ContextFile $id $file.FullName }
}

if (-not $NoSource) {
  foreach ($root in @('src', 'asio', 'vst-host', 'driver', 'sdk', 'tests')) {
    $rootPath = Join-Path $repo $root
    if (-not (Test-Path $rootPath)) { continue }
    Get-ChildItem -LiteralPath $rootPath -Recurse -File |
      Where-Object { $_.Extension -in @('.h', '.hpp', '.c', '.cpp', '.ps1', '.json') } |
      ForEach-Object { Write-ContextFile "SOURCE/$root" $_.FullName }
  }
}
