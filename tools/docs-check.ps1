[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-CounterClaims([string]$Text) {
  # Parse structural counters (required entries, specs) from BASELINE prose.
  # Volatile tracked-path / repository-JSON counts are measured live via git ls-files
  # and reported as informational output only; they are never compared against
  # committed numbers.
  $claims = [pscustomobject]@{ Required = 0; Specs = 0 }
  $patterns = [ordered]@{
    Required = @('docs-check\.ps1`\s*的\s*(?<count>\d+)\s*個必要入口', 'docs-check required-entry')
    Specs    = @('個必要入口與\s*(?<count>\d+)\s*份\s*Spec\s*通過', 'spec')
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

function Assert-StructuralClaims {
  param(
    [Parameter(Mandatory = $true)]$Claims,
    [Parameter(Mandatory = $true)][int]$Required,
    [Parameter(Mandatory = $true)][int]$Specs
  )
  if ($Claims.Required -ne $Required) {
    throw ("BASELINE.md claims {0} docs-check required entries but docs-check defines {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Required, $Required
  }
  if ($Claims.Specs -ne $Specs) {
    throw ("BASELINE.md claims {0} specs but the repository tracks {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Specs, $Specs
  }
}

function Convert-CommandOutputToText {
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Lines
  )
  if ($null -eq $Lines -or $Lines.Count -eq 0) { return '' }
  $strings = @($Lines | ForEach-Object { [string]$_ })
  return [string]::Join("`n", $strings)
}

function Test-MergeBaseMode {
  param(
    [string]$BaseRef,
    [string]$RefName
  )
  if (-not [string]::IsNullOrWhiteSpace($BaseRef)) { return $true }
  if ([string]::IsNullOrWhiteSpace($RefName)) { return $false }
  if ($RefName -eq 'main' -or $RefName -eq 'refs/heads/main' -or $RefName.EndsWith('/main')) {
    return $false
  }
  return $true
}

function Resolve-CiRefName {
  param(
    [string]$RefName,
    [string]$Ref,
    [string]$EventName,
    [string]$CurrentBranch
  )
  $resolved = ''
  if (-not [string]::IsNullOrWhiteSpace($RefName)) {
    $resolved = $RefName
  }
  elseif (-not [string]::IsNullOrWhiteSpace($Ref)) {
    $resolved = $Ref -replace '^refs/heads/', ''
  }
  if ($EventName -eq 'push' -and -not [string]::IsNullOrWhiteSpace($CurrentBranch)) {
    $resolved = $CurrentBranch
  }
  return $resolved
}

function Test-BaselineChangedByHead {
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$ChangedPaths
  )
  return @($ChangedPaths | Where-Object { $_ -eq 'docs/state/BASELINE.md' }).Count -gt 0
}

if ($SelfTest) {
  $caseCount = 0

  # Multiline normalization.
  $multilineLines = @('line-one', 'line-two', 'line-three')
  $multilineText = Convert-CommandOutputToText -Lines $multilineLines
  if ($multilineText -ne "line-one`nline-two`nline-three") {
    throw 'docs-check self-test failed: multiline command output did not normalize to text.'
  }
  $caseCount++

  # Merge-base mode detection.
  if (-not (Test-MergeBaseMode -BaseRef 'main' -RefName '')) {
    throw 'docs-check self-test failed: pull-request mode was not recognized.'
  }
  $caseCount++
  if (Test-MergeBaseMode -BaseRef '' -RefName 'main') {
    throw 'docs-check self-test failed: direct main push must remain strict.'
  }
  $caseCount++
  if (-not (Test-MergeBaseMode -BaseRef '' -RefName 'codex/feature')) {
    throw 'docs-check self-test failed: feature-branch push mode was not recognized.'
  }
  $caseCount++

  # CI ref resolution.
  if ((Resolve-CiRefName -RefName '' -Ref '' -EventName 'push' -CurrentBranch 'codex/feature') -ne 'codex/feature') {
    throw 'docs-check self-test failed: push-event checkout branch fallback was not recognized.'
  }
  $caseCount++

  # BASELINE edit detection.
  if (Test-BaselineChangedByHead -ChangedPaths @('evidence/0000-foundation/initial.json')) {
    throw 'docs-check self-test failed: a handoff-only head was treated as a BASELINE owner.'
  }
  $caseCount++
  if (-not (Test-BaselineChangedByHead -ChangedPaths @('docs/state/BASELINE.md'))) {
    throw 'docs-check self-test failed: a head BASELINE edit was not detected.'
  }
  $caseCount++

  # Structural counter parser: valid summary parses correctly.
  $summaryOk = @'
目前驗證摘要：`docs-check.ps1` 的 85 個必要入口與
24 份 Spec 通過。
'@
  $ok = Get-CounterClaims $summaryOk
  if ($ok.Required -ne 85 -or $ok.Specs -ne 24) {
    throw 'docs-check self-test failed: canonical summary did not parse to expected structural counters.'
  }
  $caseCount++

  # Removing required-entry or spec markers must fail closed.
  foreach ($fragment in @('個必要入口', '份 Spec 通過')) {
    $broken = $summaryOk.Replace($fragment, 'removed-marker')
    try { Get-CounterClaims $broken | Out-Null } catch { $caseCount++; continue }
    throw "docs-check self-test failed: removing '$fragment' should fail the counter parser."
  }

  # Structural assertion passes with matching values.
  Assert-StructuralClaims -Claims $ok -Required 85 -Specs 24
  $caseCount++

  # Live measurement: git ls-files returns at least some files in a real repo.
  $liveTracked = @(git ls-files)
  if ($liveTracked.Count -lt 1) {
    throw 'docs-check self-test failed: live git ls-files returned zero files in a real repository.'
  }
  $caseCount++
  $liveJson = @($liveTracked | Where-Object { $_.ToLowerInvariant().EndsWith('.json') })
  if ($liveJson.Count -lt 1) {
    throw 'docs-check self-test failed: live git ls-files returned zero JSON files in a real repository.'
  }
  $caseCount++

  if ($caseCount -lt 12) {
    throw "docs-check self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "docs-check self-test passed ($caseCount cases; structural parser, multiline normalization, branch mode detection, BASELINE edit detection, live measurement)."
  exit 0
}

# --- Main gate logic ---

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
  'docs/ai/MULTI_AGENT.md',
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
  'docs/VST3_STATE_COMPATIBILITY_REVIEW.md', 'evidence/0000-foundation/initial.json'
)

$missing = @($required | Where-Object { -not (Test-Path (Join-Path $repo $_)) })
if ($missing.Count -gt 0) { throw "Missing required documentation: $($missing -join ', ')" }

$handoffSchemaIndex = Get-Content -LiteralPath (Join-Path $repo 'docs/ai/HANDOFF_SCHEMA.json') -Raw |
  ConvertFrom-Json
if (-not $handoffSchemaIndex.PSObject.Properties['$comment']) {
  throw 'docs/ai/HANDOFF_SCHEMA.json must document the issue-body handoff protocol.'
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

$baselineText = Get-Content -LiteralPath (Join-Path $repo 'docs/state/BASELINE.md') -Raw
$trackedFiles = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
$jsonFiles = @(git -C $repo ls-files -- '*.json')

$claims = Get-CounterClaims $baselineText

# Structural counters (required entries and specs) are always verified against
# the tree being tested; they change rarely, so keeping them strict costs
# parallel lanes nothing.
Assert-StructuralClaims -Claims $claims -Required $required.Count -Specs $specs.Count

# Volatile volatile volatile — NO, these are measured LIVE and reported as
# informational output only. No committed counter numbers are read or compared.
$baseRef = $env:GITHUB_BASE_REF
$pullRequestMode = -not [string]::IsNullOrWhiteSpace($baseRef)

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; live measurement: {2} tracked paths, {3} repository JSON files.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
