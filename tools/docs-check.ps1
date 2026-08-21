[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-CounterClaims([string]$Text) {
  $claims = [pscustomobject]@{ Required = 0; Specs = 0; Tracked = 0; Json = 0 }
  $patterns = [ordered]@{
    Required = @('docs-check\.ps1`\s*的\s*(?<count>\d+)\s*個必要入口', 'docs-check required-entry')
    Specs    = @('個必要入口與\s*(?<count>\d+)\s*份\s*Spec\s*通過', 'spec')
    Tracked  = @('source-policy\.ps1`\s*掃描\s*(?<count>\d+)\s*個\s*tracked paths', 'source-policy tracked-path')
    Json     = @('(?<count>\d+)\s*個\s*repository JSON\s*檔案均可解析', 'repository-json')
  }
  foreach ($key in $patterns.Keys) {
    $match = [regex]::Match($Text, [string]$patterns[$key][0])
    if (-not $match.Success) {
      throw ("BASELINE.md is missing the {0} counter expected by docs-check; keep the " +
             "verification-summary sentence in docs/state/BASELINE.md up to date.") -f $patterns[$key][1]
    }
    $claims.$key = [int]$match.Groups['count'].Value
  }
  return $claims
}

function Assert-CounterClaims {
  param(
    [Parameter(Mandatory = $true)]$Claims,
    [Parameter(Mandatory = $true)][int]$Required,
    [Parameter(Mandatory = $true)][int]$Specs,
    [Parameter(Mandatory = $true)][int]$Tracked,
    [Parameter(Mandatory = $true)][int]$Json
  )
  if ($Claims.Required -ne $Required) {
    throw ("BASELINE.md claims {0} docs-check required entries but docs-check defines {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Required, $Required
  }
  if ($Claims.Specs -ne $Specs) {
    throw ("BASELINE.md claims {0} specs but the repository tracks {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Specs, $Specs
  }
  if ($Claims.Tracked -ne $Tracked) {
    throw ("BASELINE.md claims {0} tracked paths but git reports {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Tracked, $Tracked
  }
  if ($Claims.Json -ne $Json) {
    throw ("BASELINE.md claims {0} repository JSON files but git reports {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Json, $Json
  }
}
$required = @(
  'AGENTS.md', 'CLAUDE.md', 'README.md', 'CONTRIBUTING.md', 'SECURITY.md',
  '.github/PULL_REQUEST_TEMPLATE.md', '.github/ISSUE_TEMPLATE/ai-task.yml',
  'SOURCE_POLICY.md', 'THIRD_PARTY.yml', 'config/distribution-profile.yml',
  'build/toolchain-lock.yml',
  'extensions/manifest.json', 'tools/extension-check.ps1',
  'installer/HibikiSetup.ps1', 'tools/installer-check.ps1',
  'apps/control-model/Hibiki.ControlModel.csproj', 'tools/control-model-check.ps1',
  'apps/winui-shell/Hibiki.WinUI.csproj', 'tools/winui-shell-check.ps1',
  'tools/distribution-check.ps1', 'tools/source-only-ci-check.ps1', 'tools/handoff-check.ps1',
  'tools/build-preview.ps1',
  'tools/live-device-catalog-check.ps1', 'tools/live-wasapi-handoff-check.ps1',
  'tools/live-audio-session-check.ps1', 'tools/live-process-loopback-check.ps1',
  'tools/driver-source-check.ps1', 'tools/driver-signability-check.ps1',
  'schemas/release-manifest-v1.schema.json',
  'docs/START_HERE.md', 'docs/AI_HANDOFF.md', 'docs/PROJECT_MAP.md', 'docs/state/BASELINE.md',
  'docs/specs/INDEX.md', 'docs/specs/SPEC-0001-core-contracts.md',
  'docs/specs/SPEC-0002-volume-and-iso.md', 'docs/specs/SPEC-0003-virtual-endpoints-and-routing.md',
  'docs/specs/SPEC-0004-ai-handoff-and-evidence.md', 'docs/specs/SPEC-0005-source-only-paid-release.md',
  'docs/adr/0001-public-monorepo-and-component-licenses.md',
  'docs/adr/0002-virtual-endpoint-engine-boundary.md', 'docs/ai/HANDOFF_SCHEMA.json',
  'docs/ai/DOC_SCHEMA.json', 'docs/ai/CHANGE_CONTRACT.yml', 'docs/ai/CONFLICT_POLICY.md',
  'docs/ai/MULTI_AGENT.md', 'docs/tasks/active/README.md', 'docs/tasks/active/TEMPLATE.md',
  'schemas/task-handoff-v1.schema.json', 'schemas/task-handoff-v2.schema.json',
  'schemas/acoustic-anchor-v1.schema.json', 'schemas/equal-loudness-policy-v1.schema.json',
  'schemas/equal-loudness-status-v1.schema.json', 'schemas/ipc-envelope-v1.schema.json',
  'schemas/driver-control-v1.schema.json', 'schemas/ir-phase-policy-v1.schema.json',
  'schemas/audio-session-descriptor-v1.schema.json', 'schemas/program-aware-level-policy-v1.schema.json',
  'schemas/basic-noise-suppressor-policy-v1.schema.json',
  'schemas/virtual-mic-policy-v1.schema.json',
  'schemas/graph-config-v1.schema.json', 'schemas/scene-definition-v1.schema.json',
  'schemas/custom-scene-cards-v1.schema.json',
  'schemas/physical-device-catalog-v1.schema.json',
  'schemas/device-catalog-snapshot-v1.schema.json',
  'schemas/device-switch-request-v1.schema.json',
  'schemas/peq-filter-v1.schema.json',
  'schemas/calibration-response-v1.schema.json',
  'schemas/vst3-latency-alignment-v1.schema.json',
  'schemas/latency-graph-commit-v1.schema.json',
  'schemas/vst3-parameter-timeline-v1.schema.json',
  'schemas/vst3-plugin-state-v1.schema.json',
  'schemas/scene-vst3-state-binding-v1.schema.json',
  'schemas/output-fanout-plan-v1.schema.json',
  'docs/specs/SPEC-0010-winui-shell.md',
  'docs/specs/SPEC-0011-calibration-compiler.md',
  'docs/specs/SPEC-0012-vst3-latency-graph-commit.md',
  'docs/specs/SPEC-0013-session-route-rules.md', 'docs/specs/SPEC-0014-custom-scene-catalog.md',
  'docs/specs/SPEC-0015-physical-device-catalog.md', 'docs/specs/SPEC-0016-process-loopback-capture.md',
  'docs/VST3_STATE_COMPATIBILITY_REVIEW.md',
  'docs/tasks/active/0.md', 'evidence/0000-foundation/initial.json'
)

$missing = @($required | Where-Object { -not (Test-Path (Join-Path $repo $_)) })
if ($missing.Count -gt 0) { throw "Missing required documentation: $($missing -join ', ')" }

$handoffSchemaIndex = Get-Content -LiteralPath (Join-Path $repo 'docs/ai/HANDOFF_SCHEMA.json') -Raw |
  ConvertFrom-Json
$handoffSchemaRef = $handoffSchemaIndex.PSObject.Properties['$ref'].Value
if ($handoffSchemaRef -ne '../../schemas/task-handoff-v2.schema.json') {
  throw 'docs/ai/HANDOFF_SCHEMA.json must reference the canonical task-handoff-v2 schema.'
}

$specs = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/specs') -Filter 'SPEC-*.md' -File
$ids = @($specs | ForEach-Object { Select-String -LiteralPath $_.FullName -Pattern '^id:\s*(\S+)' | ForEach-Object { $_.Matches.Groups[1].Value } })
if (($ids | Sort-Object -Unique).Count -ne $ids.Count) { throw 'Duplicate Spec IDs detected.' }

& (Join-Path $repo 'tools/handoff-check.ps1')
if ($LASTEXITCODE -ne 0) { throw 'AI handoff check failed.' }

$adapters = @('AGENTS.md', 'CLAUDE.md', 'GEMINI.md', '.github/copilot-instructions.md', '.cursor/rules/project.mdc')
foreach ($adapter in $adapters) {
  if (-not (Test-Path (Join-Path $repo $adapter))) { throw "Missing AI adapter: $adapter" }
  if ($adapter -eq 'AGENTS.md') { continue }
  $adapterText = Get-Content -LiteralPath (Join-Path $repo $adapter) -Raw
  if (-not $adapterText.Contains('AGENTS.md') -or -not $adapterText.Contains('docs/START_HERE.md')) {
    throw "AI adapter is not anchored to canonical instructions: $adapter"
  }
}

if ($SelfTest) {
  $summaryOk = @'
目前驗證摘要：`docs-check.ps1` 的 85 個必要入口與
24 份 Spec 通過；`source-policy.ps1` 掃描 411 個 tracked paths 且無 blocked
binary/secret；
`distribution-check.ps1`、`driver-source-check.ps1` 與 `driver-signability-check.ps1` 通過了71 個 repository JSON 檔案均可解析。
'@
  $caseCount = 0
  $ok = Get-CounterClaims $summaryOk
  if ($ok.Required -ne 85 -or $ok.Specs -ne 24 -or $ok.Tracked -ne 411 -or $ok.Json -ne 71) {
    throw 'docs-check self-test failed: canonical summary did not parse to expected counters.'
  }
  $caseCount++
  Assert-CounterClaims -Claims $ok -Required 85 -Specs 24 -Tracked 411 -Json 71
  $caseCount++
  foreach ($fragment in @('個必要入口', '份 Spec 通過', '掃描 411 個', 'repository JSON')) {
    $broken = $summaryOk.Replace($fragment, 'removed-marker')
    try { Get-CounterClaims $broken | Out-Null } catch { $caseCount++; continue }
    throw "docs-check self-test failed: removing '$fragment' should fail the counter parser."
  }
  try {
    $drift = Get-CounterClaims $summaryOk
    Assert-CounterClaims -Claims $drift -Required 85 -Specs 24 -Tracked 409 -Json 71
  } catch {
    $caseCount++
  }
  if ($caseCount -lt 7) {
    throw "docs-check self-test failed: expected at least 7 passing cases, saw $caseCount."
  }
  Write-Output "docs-check self-test passed ($caseCount cases; parser markers, malformed text and drift detection)."
  exit 0
}

$baselineText = Get-Content -LiteralPath (Join-Path $repo 'docs/state/BASELINE.md') -Raw
$trackedFiles = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
$jsonFiles = @(git -C $repo ls-files -- '*.json')

$claims = Get-CounterClaims $baselineText

# Structural counters (required entries and specs) are always verified against
# the tree being tested; they change rarely, so keeping them strict costs
# parallel lanes nothing.
if ($claims.Required -ne $required.Count) {
  throw ("BASELINE.md claims {0} docs-check required entries but docs-check defines {1}; " +
         "update the BASELINE.md verification summary.") -f $claims.Required, $required.Count
}
if ($claims.Specs -ne $specs.Count) {
  throw ("BASELINE.md claims {0} specs but the repository tracks {1}; " +
         "update the BASELINE.md verification summary.") -f $claims.Specs, $specs.Count
}

$baseRef = $env:GITHUB_BASE_REF
$pullRequestMode = -not [string]::IsNullOrWhiteSpace($baseRef)
if (-not $pullRequestMode) {
  # Push-to-main and local runs stay fully strict so main cannot drift silently.
  Assert-CounterClaims -Claims $claims -Required $required.Count -Specs $specs.Count `
    -Tracked $trackedFiles.Count -Json $jsonFiles.Count
  $summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; baseline summary verified against {2} tracked paths and {3} repository JSON files.)'
  Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
  exit 0
}

$baseRefName = 'origin/' + $baseRef
git -C $repo cat-file -e ("{0}^{{commit}}" -f $baseRefName) 2>$null
if ($LASTEXITCODE -ne 0) {
  throw "docs-check could not resolve merge base ref '$baseRefName'; ensure checkout keeps fetch-depth: 0."
}
$baseTracked = @(git -C $repo ls-tree -r --name-only $baseRefName)
if ($LASTEXITCODE -ne 0) { throw "docs-check could not list the merge base tree '$baseRefName'." }
$baseJson = @(git -C $repo ls-tree -r --name-only $baseRefName -- '*.json')
$baseBaselineText = git -C $repo show ('{0}:docs/state/BASELINE.md' -f $baseRefName)
if ($LASTEXITCODE -ne 0) { throw "docs-check could not read BASELINE.md from '$baseRefName'." }

# The merge base itself must be internally consistent: a stale summary on main
# is an integrator problem and fails closed here instead of blaming the PR.
$baseClaims = Get-CounterClaims $baseBaselineText
if ($baseClaims.Tracked -ne $baseTracked.Count) {
  throw ("BASELINE.md on '{0}' claims {1} tracked paths but that tree has {2}; " +
         "refresh docs/state/BASELINE.md on the target branch.") -f $baseRefName, $baseClaims.Tracked, $baseTracked.Count
}
if ($baseClaims.Json -ne $baseJson.Count) {
  throw ("BASELINE.md on '{0}' claims {1} repository JSON files but that tree has {2}; " +
         "refresh docs/state/BASELINE.md on the target branch.") -f $baseRefName, $baseClaims.Json, $baseJson.Count
}

$headNormalized = $baselineText -replace "`r", ''
$baseNormalized = $baseBaselineText -replace "`r", ''
if ($headNormalized.Trim() -eq $baseNormalized.Trim()) {
  # The PR did not touch BASELINE.md, so its own file additions are exactly the
  # counter drift the shared snapshot cannot track per-slice. The base was just
  # proven consistent; tolerate the head-side delta instead of serializing every
  # parallel lane behind an integrator refresh.
  $summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; BASELINE.md untouched by this pull request, verified against merge base {2}: {3} tracked paths and {4} repository JSON files.)'
  Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $baseRefName, $baseTracked.Count, $baseJson.Count)
  exit 0
}

# A PR that edits BASELINE.md owns its numbers end to end.
Assert-CounterClaims -Claims $claims -Required $required.Count -Specs $specs.Count `
  -Tracked $trackedFiles.Count -Json $jsonFiles.Count

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; BASELINE.md updated by this pull request, verified against head: {2} tracked paths and {3} repository JSON files.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
