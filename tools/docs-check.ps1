[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
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

function Get-BaselineClaim {
  param([string]$Text, [string]$Pattern, [string]$Label)
  $match = [regex]::Match($Text, $Pattern)
  if (-not $match.Success) {
    throw ("BASELINE.md is missing the {0} counter expected by docs-check; keep the " +
           "verification-summary sentence in docs/state/BASELINE.md up to date.") -f $Label
  }
  return [int]$match.Groups['count'].Value
}

function Test-BaselineSummary {
  param(
    [string]$Text,
    [int]$RequiredEntries,
    [int]$SpecCount,
    [int]$TrackedPaths,
    [int]$JsonFiles
  )
  $claimedRequired = Get-BaselineClaim $Text 'docs-check\.ps1`\s*的\s*(?<count>\d+)\s*個必要入口' 'docs-check required-entry'
  if ($claimedRequired -ne $RequiredEntries) {
    throw ("BASELINE.md claims {0} docs-check required entries but docs-check defines {1}; " +
           "update the BASELINE.md verification summary.") -f $claimedRequired, $RequiredEntries
  }
  $claimedSpecs = Get-BaselineClaim $Text '個必要入口與\s*(?<count>\d+)\s*份\s*Spec\s*通過' 'spec'
  if ($claimedSpecs -ne $SpecCount) {
    throw ("BASELINE.md claims {0} specs but the repository tracks {1}; " +
           "update the BASELINE.md verification summary.") -f $claimedSpecs, $SpecCount
  }
  $claimedTracked = Get-BaselineClaim $Text 'source-policy\.ps1`\s*掃描\s*(?<count>\d+)\s*個\s*tracked paths' 'source-policy tracked-path'
  if ($claimedTracked -ne $TrackedPaths) {
    throw ("BASELINE.md claims {0} tracked paths but git reports {1}; " +
           "update the BASELINE.md verification summary.") -f $claimedTracked, $TrackedPaths
  }
  $claimedJson = Get-BaselineClaim $Text '(?<count>\d+)\s*個\s*repository JSON\s*檔案均可解析' 'repository-json'
  if ($claimedJson -ne $JsonFiles) {
    throw ("BASELINE.md claims {0} repository JSON files but git reports {1}; " +
           "update the BASELINE.md verification summary.") -f $claimedJson, $JsonFiles
  }
}

if ($SelfTest) {
  $sampleTemplate = '目前驗證摘要：`verify.ps1` 的 1 個 CTest 通過；`docs-check.ps1` 的 {0} 個必要入口與{1}份 Spec 通過；`source-policy.ps1` 掃描 {2} 個 tracked paths 且無 blocked binary/secret；通過；{3} 個 repository JSON 檔案均可解析。'

  function Assert-GateRejection {
    param([scriptblock]$Action, [string]$ExpectedPattern, [string]$Label)
    try { & $Action } catch {
      if ("$($_.Exception.Message)" -notmatch $ExpectedPattern) {
        throw ("docs-check self-test case '{0}' failed with an unexpected message: {1}") -f $Label, $_.Exception.Message
      }
      return
    }
    throw ("docs-check self-test case '{0}' expected a rejection matching '{1}' but the gate passed.") -f $Label, $ExpectedPattern
  }

  $caseCount = 0

  Test-BaselineSummary -Text ($sampleTemplate -f 85, 24, 397, 65) -RequiredEntries 85 -SpecCount 24 -TrackedPaths 397 -JsonFiles 65
  $caseCount++
  Test-BaselineSummary -Text ($sampleTemplate -f 96, 35, 410, 77) -RequiredEntries 96 -SpecCount 35 -TrackedPaths 410 -JsonFiles 77
  $caseCount++

  Assert-GateRejection -Label 'required-entries-mismatch' -ExpectedPattern 'claims 85 docs-check required entries but docs-check defines 86' `
    -Action { Test-BaselineSummary -Text ($sampleTemplate -f 85, 24, 397, 65) -RequiredEntries 86 -SpecCount 24 -TrackedPaths 397 -JsonFiles 65 }
  $caseCount++
  Assert-GateRejection -Label 'spec-count-mismatch' -ExpectedPattern 'claims 24 specs but the repository tracks 25' `
    -Action { Test-BaselineSummary -Text ($sampleTemplate -f 85, 24, 397, 65) -RequiredEntries 85 -SpecCount 25 -TrackedPaths 397 -JsonFiles 65 }
  $caseCount++
  Assert-GateRejection -Label 'tracked-paths-mismatch' -ExpectedPattern 'claims 397 tracked paths but git reports 401' `
    -Action { Test-BaselineSummary -Text ($sampleTemplate -f 85, 24, 397, 65) -RequiredEntries 85 -SpecCount 24 -TrackedPaths 401 -JsonFiles 65 }
  $caseCount++
  Assert-GateRejection -Label 'repository-json-mismatch' -ExpectedPattern 'claims 65 repository JSON files but git reports 66' `
    -Action { Test-BaselineSummary -Text ($sampleTemplate -f 85, 24, 397, 65) -RequiredEntries 85 -SpecCount 24 -TrackedPaths 397 -JsonFiles 66 }
  $caseCount++
  Assert-GateRejection -Label 'missing-all-markers' -ExpectedPattern 'missing the docs-check required-entry counter' `
    -Action { Test-BaselineSummary -Text 'no verification summary here' -RequiredEntries 85 -SpecCount 24 -TrackedPaths 397 -JsonFiles 65 }
  $caseCount++
  Assert-GateRejection -Label 'missing-tracked-path-marker' -ExpectedPattern 'missing the source-policy tracked-path counter' `
    -Action { Test-BaselineSummary -Text (($sampleTemplate -f 85, 24, 397, 65) -replace '；`source-policy\.ps1` 掃描 397 個 tracked paths 且無 blocked binary/secret；', '；') -RequiredEntries 85 -SpecCount 24 -TrackedPaths 397 -JsonFiles 65 }
  $caseCount++

  Write-Output "Documentation baseline-summary gate self-test passed ($caseCount cases)."
  exit 0
}

$baselineText = Get-Content -LiteralPath (Join-Path $repo 'docs/state/BASELINE.md') -Raw
$trackedFiles = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
$jsonFiles = @(git -C $repo ls-files -- '*.json')

Test-BaselineSummary -Text $baselineText -RequiredEntries $required.Count -SpecCount $specs.Count -TrackedPaths $trackedFiles.Count -JsonFiles $jsonFiles.Count

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; baseline summary verified against {2} tracked paths and {3} repository JSON files.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
