[CmdletBinding()]
param(
  [switch]$SelfTest,
  [switch]$WriteCounters
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-CounterClaims([string]$Text) {
  # Parse only structural counters (required entries, specs) from BASELINE prose.
  # Volatile tracked-path / repository-JSON counts live in build/baseline-counters.json.
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

function Read-BaselineCountersJson {
  param([Parameter(Mandatory = $true)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "build/baseline-counters.json is missing; run: pwsh -File tools/docs-check.ps1 -WriteCounters"
  }
  try {
    $json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  } catch {
    throw "build/baseline-counters.json is not valid JSON: $($_.Exception.Message)"
  }
  if ($json.schema_version -ne 1) {
    throw "build/baseline-counters.json has schema_version '$($json.schema_version)' but this gate requires '1'."
  }
  foreach ($key in @('tracked_paths', 'repository_json_files')) {
    if ($null -eq $json.$key -or -not ($json.$key -is [int] -or $json.$key -is [long]) -or $json.$key -lt 0) {
      throw "build/baseline-counters.json is missing or has an invalid non-negative integer '$key'."
    }
  }
  return [pscustomobject]@{ Tracked = [int]$json.tracked_paths; Json = [int]$json.repository_json_files }
}

function Write-BaselineCountersJson {
  param(
    [Parameter(Mandatory = $true)][int]$Tracked,
    [Parameter(Mandatory = $true)][int]$Json
  )
  $path = Join-Path $repo 'build/baseline-counters.json'
  $content = @{
    schema_version        = 1
    tracked_paths         = $Tracked
    repository_json_files = $Json
  } | ConvertTo-Json
  [System.IO.File]::WriteAllText($path, $content + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
  Write-Output "Updated build/baseline-counters.json (tracked_paths=$Tracked repository_json_files=$Json)."
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
  if ($Tracked -ne $trackedFiles.Count) {
    throw ("build/baseline-counters.json claims {0} tracked paths but git reports {1}; " +
           "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $Tracked, $trackedFiles.Count
  }
  if ($Json -ne $jsonFiles.Count) {
    throw ("build/baseline-counters.json claims {0} repository JSON files but git reports {1}; " +
           "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $Json, $jsonFiles.Count
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
24 份 Spec 通過；`source-policy.ps1` 掃描 tracked paths 且無 blocked binary/secret；
`distribution-check.ps1`、`driver-source-check.ps1` 與 `driver-signability-check.ps1` 通過了 repository JSON 檔案均可解析。
'@
  $caseCount = 0
  $multilineLines = @('line-one', 'line-two', 'line-three')
  $multilineText = Convert-CommandOutputToText -Lines $multilineLines
  if ($multilineText -ne "line-one`nline-two`nline-three") {
    throw 'docs-check self-test failed: multiline command output did not normalize to text.'
  }
  $caseCount++
  $sameBaseline = Convert-CommandOutputToText -Lines @('BASELINE line one', 'BASELINE line two')
  $sameHead = "BASELINE line one`nBASELINE line two"
  if (($sameBaseline -replace "`r", '').Trim() -ne $sameHead.Trim()) {
    throw 'docs-check self-test failed: equal multiline head/base text was not recognized.'
  }
  $caseCount++
  $changedBaseline = Convert-CommandOutputToText -Lines @('BASELINE line one', 'BASELINE changed')
  if (($changedBaseline -replace "`r", '').Trim() -eq $sameHead.Trim()) {
    throw 'docs-check self-test failed: changed multiline head/base text was treated as equal.'
  }
  $caseCount++
  if (-not (Test-MergeBaseMode -BaseRef 'main' -RefName '')) {
    throw 'docs-check self-test failed: pull-request mode was not recognized.'
  }
  $caseCount++
  if (-not (Test-MergeBaseMode -BaseRef '' -RefName 'codex/feature')) {
    throw 'docs-check self-test failed: feature-branch push mode was not recognized.'
  }
  $caseCount++
  if ((Resolve-CiRefName -RefName '' -Ref '' -EventName 'push' -CurrentBranch 'codex/feature') -ne 'codex/feature') {
    throw 'docs-check self-test failed: push-event checkout branch fallback was not recognized.'
  }
  $caseCount++
  if ((Resolve-CiRefName -RefName 'main' -Ref '' -EventName 'push' -CurrentBranch 'codex/feature') -ne 'codex/feature') {
    throw 'docs-check self-test failed: push-event checkout branch did not override a misleading ref name.'
  }
  $caseCount++
  if (Test-BaselineChangedByHead -ChangedPaths @('docs/tasks/active/64.md')) {
    throw 'docs-check self-test failed: a handoff-only head was treated as a BASELINE owner.'
  }
  $caseCount++
  if (-not (Test-BaselineChangedByHead -ChangedPaths @('docs/state/BASELINE.md'))) {
    throw 'docs-check self-test failed: a head BASELINE edit was not detected.'
  }
  $caseCount++
  if (Test-MergeBaseMode -BaseRef '' -RefName 'main') {
    throw 'docs-check self-test failed: direct main push must remain strict.'
  }
  $caseCount++

  # Prose parser: Required and Specs still parse from BASELINE.md.
  $ok = Get-CounterClaims $summaryOk
  if ($ok.Required -ne 85 -or $ok.Specs -ne 24) {
    throw 'docs-check self-test failed: canonical summary did not parse to expected structural counters.'
  }
  $caseCount++

  # Removing required-entry or spec markers must still fail closed.
  foreach ($fragment in @('個必要入口', '份 Spec 通過')) {
    $broken = $summaryOk.Replace($fragment, 'removed-marker')
    try { Get-CounterClaims $broken | Out-Null } catch { $caseCount++; continue }
    throw "docs-check self-test failed: removing '$fragment' should fail the counter parser."
  }

  # JSON counter source-of-truth fixtures.
  $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("hibiki-selftest-" + [guid]::NewGuid().ToString("N").Substring(0,8))
  New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
  try {
    $countersPath = Join-Path $tempDir 'baseline-counters.json'

    # Case: missing JSON file fails closed.
    try { Read-BaselineCountersJson -Path $countersPath | Out-Null } catch { $caseCount++ }

    # Case: malformed JSON fails closed.
    Set-Content -LiteralPath $countersPath -Value '{ broken' -Encoding UTF8NoBOM
    try { Read-BaselineCountersJson -Path $countersPath | Out-Null } catch { $caseCount++ }

    # Case: schema_version drift fails closed.
    Set-Content -LiteralPath $countersPath -Value '{"schema_version":2,"tracked_paths":10,"repository_json_files":5}' -Encoding UTF8NoBOM
    try { Read-BaselineCountersJson -Path $countersPath | Out-Null } catch { $caseCount++ }

    # Case: matching counters parse correctly.
    Set-Content -LiteralPath $countersPath -Value '{"schema_version":1,"tracked_paths":468,"repository_json_files":98}' -Encoding UTF8NoBOM
    $readOk = Read-BaselineCountersJson -Path $countersPath
    if ($readOk.Tracked -ne 468 -or $readOk.Json -ne 98) {
      throw 'docs-check self-test failed: matching JSON counters did not round-trip.'
    }
    $caseCount++

    # Case: negative integer fails closed.
    Set-Content -LiteralPath $countersPath -Value '{"schema_version":1,"tracked_paths":-1,"repository_json_files":5}' -Encoding UTF8NoBOM
    try { Read-BaselineCountersJson -Path $countersPath | Out-Null } catch { $caseCount++ }
  } finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
  }

  if ($caseCount -lt 18) {
    throw "docs-check self-test failed: expected at least 18 passing cases, saw $caseCount."
  }
  Write-Output "docs-check self-test passed ($caseCount cases; prose parser, multiline normalization, branch mode and JSON counter fixtures)."
  exit 0
}

# Handle -WriteCounters: mechanically refresh the JSON from git and exit.
if ($WriteCounters) {
  $trackedFilesNow = @(git -C $repo ls-files)
  if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
  $jsonFilesNow = @(git -C $repo ls-files -- '*.json')
  Write-BaselineCountersJson -Tracked $trackedFilesNow.Count -Json $jsonFilesNow.Count
  exit 0
}

$baselineText = Get-Content -LiteralPath (Join-Path $repo 'docs/state/BASELINE.md') -Raw
$trackedFiles = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
$jsonFiles = @(git -C $repo ls-files -- '*.json')

$claims = Get-CounterClaims $baselineText

# Volatile counters come exclusively from build/baseline-counters.json.
$fileCounters = Read-BaselineCountersJson -Path (Join-Path $repo 'build/baseline-counters.json')

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
$currentBranch = if ($env:GITHUB_EVENT_NAME -eq 'push') {
  (git -C $repo branch --show-current 2>$null).Trim()
} else {
  ''
}
$refName = Resolve-CiRefName -RefName $env:GITHUB_REF_NAME -Ref $env:GITHUB_REF `
  -EventName $env:GITHUB_EVENT_NAME -CurrentBranch $currentBranch
$pullRequestMode = -not [string]::IsNullOrWhiteSpace($baseRef)
$mergeBaseMode = Test-MergeBaseMode -BaseRef $baseRef -RefName $refName
if (-not $mergeBaseMode) {
  # Push-to-main and local runs stay fully strict so main cannot drift silently.
  if ($fileCounters.Tracked -ne $trackedFiles.Count) {
    throw ("build/baseline-counters.json claims {0} tracked paths but git reports {1}; " +
           "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $fileCounters.Tracked, $trackedFiles.Count
  }
  if ($fileCounters.Json -ne $jsonFiles.Count) {
    throw ("build/baseline-counters.json claims {0} repository JSON files but git reports {1}; " +
           "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $fileCounters.Json, $jsonFiles.Count
  }
  $summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; baseline summary verified against {2} tracked paths and {3} repository JSON files.)'
  Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
  exit 0
}

$baseRefName = if ($pullRequestMode) { 'origin/' + $baseRef } else { 'origin/main' }
git -C $repo cat-file -e ("{0}^{{commit}}" -f $baseRefName) 2>$null
if ($LASTEXITCODE -ne 0) {
  throw "docs-check could not resolve merge base ref '$baseRefName'; ensure checkout keeps fetch-depth: 0."
}
$mergeBase = (git -C $repo merge-base HEAD $baseRefName 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($mergeBase)) {
  throw "docs-check could not resolve the common ancestor of HEAD and '$baseRefName'."
}
$headBaselineChanges = @(git -C $repo diff --name-only $mergeBase HEAD -- docs/state/BASELINE.md)
if ($LASTEXITCODE -ne 0) {
  throw "docs-check could not determine whether this head edits docs/state/BASELINE.md."
}
$baselineChangedByHead = Test-BaselineChangedByHead -ChangedPaths $headBaselineChanges

$headCountersChanges = @(git -C $repo diff --name-only $mergeBase HEAD -- build/baseline-counters.json)
if ($LASTEXITCODE -ne 0) {
  throw "docs-check could not determine whether this head edits build/baseline-counters.json."
}
$countersChangedByHead = @($headCountersChanges | Where-Object { $_ -eq 'build/baseline-counters.json' }).Count -gt 0

$baseTracked = @(git -C $repo ls-tree -r --name-only $baseRefName)
if ($LASTEXITCODE -ne 0) { throw "docs-check could not list the merge base tree '$baseRefName'." }
# git ls-tree does not expand a bare '*.json' pathspec across directories the way
# git ls-files does; filter the full listing instead of trusting a pathspec.
$baseJson = @($baseTracked | Where-Object { $_.ToLowerInvariant().EndsWith('.json') })

# The merge base itself must be internally consistent: stale counters on main
# are an integrator problem and fail closed here instead of blaming the PR.
if ($countersChangedByHead) {
  # This PR introduces build/baseline-counters.json for the first time; the merge
  # base predates the file, so there are no base counters to verify.
  $summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; baseline-counters.json introduced by this pull request and verified against head: {2} tracked paths and {3} repository JSON files.)'
  Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
  exit 0
}
$baseCountersLines = @(git -C $repo show ('{0}:build/baseline-counters.json' -f $baseRefName))
if ($LASTEXITCODE -ne 0) { throw "docs-check could not read baseline-counters.json from '$baseRefName'; this file must exist on main before other PRs can rely on it." }
$baseCountersText = Convert-CommandOutputToText -Lines $baseCountersLines
$tempBasePath = Join-Path ([System.IO.Path]::GetTempPath()) ("hibiki-base-counters-" + [guid]::NewGuid().ToString("N").Substring(0,8) + ".json")
try {
  Set-Content -LiteralPath $tempBasePath -Value $baseCountersText -Encoding UTF8NoBOM
  $baseFileCounters = Read-BaselineCountersJson -Path $tempBasePath
} finally {
  Remove-Item -LiteralPath $tempBasePath -Force -ErrorAction SilentlyContinue
}
if ($baseFileCounters.Tracked -ne $baseTracked.Count) {
  throw ("build/baseline-counters.json on '{0}' claims {1} tracked paths but that tree has {2}; " +
         "refresh build/baseline-counters.json on the target branch.") -f $baseRefName, $baseFileCounters.Tracked, $baseTracked.Count
}
if ($baseFileCounters.Json -ne $baseJson.Count) {
  throw ("build/baseline-counters.json on '{0}' claims {1} repository JSON files but that tree has {2}; " +
         "refresh build/baseline-counters.json on the target branch.") -f $baseRefName, $baseFileCounters.Json, $baseJson.Count
}

$baselineLines = @(git -C $repo show ('{0}:docs/state/BASELINE.md' -f $baseRefName))
if ($LASTEXITCODE -ne 0) { throw "docs-check could not read BASELINE.md from '$baseRefName'." }
$baseBaselineText = Convert-CommandOutputToText -Lines $baselineLines

$headNormalized = $baselineText -replace "`r", ''
$baseNormalized = $baseBaselineText -replace "`r", ''
if (-not $baselineChangedByHead -and -not $countersChangedByHead) {
  # Neither BASELINE prose nor counters were touched by this PR. The merge base's
  # internal consistency was already verified above; this head inherits it.
  $normalizationState = if ($headNormalized.Trim() -eq $baseNormalized.Trim()) { 'equal' } else { 'parallel-main-drift' }
  $summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; BASELINE.md untouched by this pull request, normalized comparison={2}, verified against merge base {3}: {4} tracked paths and {5} repository JSON files.)'
  Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $normalizationState, $baseRefName, $baseTracked.Count, $baseJson.Count)
  exit 0
}

# A PR that edits counters owns its numbers end to end against its own head tree.
if ($fileCounters.Tracked -ne $trackedFiles.Count) {
  throw ("build/baseline-counters.json claims {0} tracked paths but git reports {1}; " +
         "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $fileCounters.Tracked, $trackedFiles.Count
}
if ($fileCounters.Json -ne $jsonFiles.Count) {
  throw ("build/baseline-counters.json claims {0} repository JSON files but git reports {1}; " +
         "run: pwsh -File tools/docs-check.ps1 -WriteCounters") -f $fileCounters.Json, $jsonFiles.Count
}

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; baseline-counters.json updated by this pull request, verified against head: {2} tracked paths and {3} repository JSON files.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count)
