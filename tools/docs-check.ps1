[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

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

function ConvertFrom-AdrFrontmatter([string]$RawText) {
  # Parse the comment-style frontmatter block used by all ADR files.
  # Returns a hashtable of key -> string value, or throws on structural errors.
  $lines = @($RawText -split "`r?`n")
  if ($lines.Count -lt 3) { throw 'ADR file is too short to contain frontmatter.' }
  if ($lines[0] -ne '# ---') { throw 'ADR frontmatter must start with "# ---" on line 1.' }
  $endIndex = -1
  for ($i = 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq '# ---') { $endIndex = $i; break }
  }
  if ($endIndex -lt 2) { throw 'ADR frontmatter closing "# ---" not found or block is empty.' }

  $fields = @{}
  for ($i = 1; $i -lt $endIndex; $i++) {
    $line = $lines[$i]
    if (-not $line.StartsWith('# ')) { throw "ADR frontmatter line $($_ + 1) must start with '# ': $line" }
    $content = $line.Substring(2).Trim()
    if ([string]::IsNullOrEmpty($content)) { continue }
    $colonIdx = $content.IndexOf(':')
    if ($colonIdx -lt 1) { throw "ADR frontmatter entry lacks a key:value separator: $content" }
    $key = $content.Substring(0, $colonIdx).Trim()
    $value = $content.Substring($colonIdx + 1).Trim()
    if ($fields.ContainsKey($key)) { throw "Duplicate ADR frontmatter key: $key" }
    $fields[$key] = $value
  }
  return $fields
}

function Assert-AdrFrontmatter {
  param(
    [Parameter(Mandatory = $true)]$Fields,
    [Parameter(Mandatory = $true)][string]$FileName
  )
  foreach ($key in @('id', 'status', 'owner', 'authority', 'date', 'last_reviewed', 'review_after_days', 'source_globs')) {
    if (-not $Fields.ContainsKey($key) -or [string]::IsNullOrWhiteSpace([string]$Fields[$key])) {
      throw "$FileName is missing required ADR frontmatter field: $key"
    }
  }

  $allowedAuthority = @('workflow', 'product-behavior', 'architecture', 'current-state', 'task', 'evidence')
  if ($allowedAuthority -notcontains $Fields['authority']) {
    throw "$FileName authority '$($Fields['authority'])' is not in the allowed set."
  }

  $days = 0
  if (-not [int]::TryParse([string]$Fields['review_after_days'], [ref]$days) -or $days -lt 1) {
    throw "$FileName review_after_days must be a positive integer."
  }

  $parsedDate = [datetime]::MinValue
  foreach ($dateKey in @('date', 'last_reviewed')) {
    if (-not [datetime]::TryParseExact([string]$Fields[$dateKey], 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
      throw "$FileName $dateKey must be formatted as yyyy-MM-dd."
    }
  }
}

function ConvertFrom-SpecFrontmatter([string]$RawText) {
  # Parse the YAML-style frontmatter block used by all Spec files.
  # Returns a hashtable of key -> string value, or throws on structural errors.
  $lines = @($RawText -split "`r?`n")
  if ($lines.Count -lt 3) { throw 'Spec file is too short to contain frontmatter.' }
  if ($lines[0] -ne '---') { throw 'Spec frontmatter must start with "---" on line 1.' }
  $endIndex = -1
  for ($i = 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq '---') { $endIndex = $i; break }
  }
  if ($endIndex -lt 2) { throw 'Spec frontmatter closing "---" not found or block is empty.' }

  $fields = @{}
  for ($i = 1; $i -lt $endIndex; $i++) {
    $line = $lines[$i]
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $colonIdx = $line.IndexOf(':')
    if ($colonIdx -lt 1) { throw "Spec frontmatter line $($i + 1) lacks a key:value separator: $line" }
    $key = $line.Substring(0, $colonIdx).Trim()
    $value = $line.Substring($colonIdx + 1).Trim()
    if ($fields.ContainsKey($key)) { throw "Duplicate Spec frontmatter key: $key" }
    $fields[$key] = $value
  }
  return $fields
}

function Assert-SpecFrontmatter {
  param(
    [Parameter(Mandatory = $true)]$Fields,
    [Parameter(Mandatory = $true)][string]$FileName
  )
  foreach ($key in @('id', 'status', 'owner', 'authority', 'last_reviewed', 'review_after_days', 'related_adrs', 'source_globs')) {
    if (-not $Fields.ContainsKey($key) -or [string]::IsNullOrWhiteSpace([string]$Fields[$key])) {
      throw "$FileName is missing required Spec frontmatter field: $key"
    }
  }

  $allowedStatus = @('accepted', 'draft')
  if ($allowedStatus -notcontains $Fields['status']) {
    throw "$FileName status '$($Fields['status'])' is not in the allowed set."
  }

  $allowedAuthority = @('product-behavior', 'repository-process', 'release-policy', 'architecture', 'platform-boundary', 'control-plane', 'control-model')
  if ($allowedAuthority -notcontains $Fields['authority']) {
    throw "$FileName authority '$($Fields['authority'])' is not in the allowed set."
  }

  $days = 0
  if (-not [int]::TryParse([string]$Fields['review_after_days'], [ref]$days) -or $days -lt 1) {
    throw "$FileName review_after_days must be a positive integer."
  }

  $parsedDate = [datetime]::MinValue
  if (-not [datetime]::TryParseExact([string]$Fields['last_reviewed'], 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
    throw "$FileName last_reviewed must be formatted as yyyy-MM-dd."
  }
}

function Test-MarkdownRelativeLinks {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [AllowEmptyString()] [string[]] $Lines,
    [Parameter(Mandatory)] [string] $BaseDir
  )
  $broken = @()
  $checked = 0
  $inFence = $false
  foreach ($line in $lines) {
    if ($line -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
    if ($inFence) { continue }
    $pos = 0
    while ($true) {
      $idx = $line.IndexOf('](', $pos)
      if ($idx -lt 0) { break }
      $open = $line.LastIndexOf('[', $idx)
      if ($open -ge 0) {
        $rest = $line.Substring($idx + 2)
        $end = $rest.IndexOf(')')
        if ($end -gt 0) {
          $target = $rest.Substring(0, $end).Trim().Trim('<>').Split(' ')[0]
          $checked++
          $isExternal = $target.StartsWith('http://') -or $target.StartsWith('https://') -or $target.StartsWith('mailto:') -or $target.StartsWith('#')
          if (-not $isExternal) {
            $pathPart = $target.Split('#')[0]
            if ($pathPart) {
              try {
                $decoded = [System.Uri]::UnescapeDataString($pathPart)
                $resolved = Join-Path $BaseDir $decoded
                if (-not (Test-Path -LiteralPath $resolved)) { $broken += $target }
              } catch {
                $broken += $target
              }
            }
          }
        }
      }
      $pos = $idx + 2
    }
  }
  return @{ Broken = $broken; Checked = $checked }
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

  # ADR frontmatter: valid block parses correctly.
  $validAdr = "# ---`n# id: ADR-0001`n# status: accepted`n# owner: hibiki-maintainers`n# authority: architecture`n# date: 2026-08-21`n# last_reviewed: 2026-08-21`n# review_after_days: 90`n# supersedes: []`n# related_specs: []`n# source_globs: [`"src/**`"]`n# ---`n`n# Title`n`nBody text."
  $parsedAdr = ConvertFrom-AdrFrontmatter -RawText $validAdr
  if ($parsedAdr['id'] -ne 'ADR-0001' -or $parsedAdr['authority'] -ne 'architecture') {
    throw 'docs-check self-test failed: valid ADR frontmatter did not parse to expected fields.'
  }
  Assert-AdrFrontmatter -Fields $parsedAdr -FileName 'selftest-valid.md'
  $caseCount++

  # ADR frontmatter: missing opening marker must fail.
  try { ConvertFrom-AdrFrontmatter -RawText "no frontmatter here`njust text" | Out-Null } catch { $caseCount++; continue }
  throw 'docs-check self-test failed: missing frontmatter opener should fail.'

  # ADR frontmatter: missing required field must fail.
  $missingField = "# ---`n# id: ADR-0002`n# status: accepted`n# ---`ntext"
  $parsedMissing = ConvertFrom-AdrFrontmatter -RawText $missingField
  $caughtMissing = $false
  try { Assert-AdrFrontmatter -Fields $parsedMissing -FileName 'selftest-missing.md' | Out-Null } catch { $caughtMissing = $true }
  if (-not $caughtMissing) { throw 'docs-check self-test failed: missing required field should fail.' }
  $caseCount++

  # ADR frontmatter: bad authority must fail.
  $badAuthority = "# ---`n# id: ADR-0003`n# status: accepted`n# owner: x`n# authority: nonsense`n# date: 2026-08-21`n# last_reviewed: 2026-08-21`n# review_after_days: 30`n# source_globs: []`n# ---`nbody"
  $parsedBadAuth = ConvertFrom-AdrFrontmatter -RawText $badAuthority
  $caughtBadAuth = $false
  try { Assert-AdrFrontmatter -Fields $parsedBadAuth -FileName 'selftest-badauth.md' | Out-Null } catch { $caughtBadAuth = $true }
  if (-not $caughtBadAuth) { throw 'docs-check self-test failed: invalid authority should fail.' }
  $caseCount++

  # Spec frontmatter: valid block parses correctly.
  $validSpec = "---`nid: SPEC-0001`nstatus: accepted`nowner: hibiki-maintainers`nauthority: product-behavior`nlast_reviewed: 2026-08-21`nreview_after_days: 30`nrelated_adrs: [ADR-0001]`nsource_globs: [`"src/**`"]`n---`n`n# SPEC-0001`n`nBody."
  $parsedSpec = ConvertFrom-SpecFrontmatter -RawText $validSpec
  if ($parsedSpec['id'] -ne 'SPEC-0001' -or $parsedSpec['authority'] -ne 'product-behavior') {
    throw 'docs-check self-test failed: valid Spec frontmatter did not parse to expected fields.'
  }
  Assert-SpecFrontmatter -Fields $parsedSpec -FileName 'selftest-spec-valid.md'
  $caseCount++

  # Spec frontmatter: missing opening marker must fail.
  try { ConvertFrom-SpecFrontmatter -RawText "no frontmatter here\njust text" | Out-Null } catch { $caseCount++; continue }
  throw 'docs-check self-test failed: missing Spec frontmatter opener should fail.'

  # Spec frontmatter: missing required field must fail.
  $missingSpecField = "---`nid: SPEC-0002`nstatus: accepted`n---`ntext"
  $parsedMissingSpec = ConvertFrom-SpecFrontmatter -RawText $missingSpecField
  $caughtMissingSpec = $false
  try { Assert-SpecFrontmatter -Fields $parsedMissingSpec -FileName 'selftest-spec-missing.md' | Out-Null } catch { $caughtMissingSpec = $true }
  if (-not $caughtMissingSpec) { throw 'docs-check self-test failed: missing required Spec field should fail.' }
  $caseCount++

  # Spec frontmatter: bad authority must fail.
  $badSpecAuth = "---`nid: SPEC-0003`nstatus: accepted`nowner: x`nauthority: nonsense`nlast_reviewed: 2026-08-21`nreview_after_days: 30`nrelated_adrs: []`nsource_globs: []`n---`nbody"
  $parsedBadSpecAuth = ConvertFrom-SpecFrontmatter -RawText $badSpecAuth
  $caughtBadSpecAuth = $false
  try { Assert-SpecFrontmatter -Fields $parsedBadSpecAuth -FileName 'selftest-spec-badauth.md' | Out-Null } catch { $caughtBadSpecAuth = $true }
  if (-not $caughtBadSpecAuth) { throw 'docs-check self-test failed: invalid Spec authority should fail.' }
  $caseCount++

  # Markdown relative links: valid target passes and is counted.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('see [handoff](docs/AI_HANDOFF.md)') -BaseDir $repo
  if ($linkResult.Checked -ne 1 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: valid relative link was not resolved.'
  }
  $caseCount++

  # Markdown relative links: missing target must be reported.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('see [ghost](docs/does-not-exist.md)') -BaseDir $repo
  if ($linkResult.Broken.Count -ne 1) {
    throw 'docs-check self-test failed: missing link target should be reported.'
  }
  $caseCount++

  # Markdown relative links: absolute URLs and anchors are skipped.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('[site](https://example.com/a) [mail](mailto:x@y.z) [anchor](#section)') -BaseDir $repo
  if ($linkResult.Checked -ne 3 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: external targets should be skipped without breakage.'
  }
  $caseCount++

  # Markdown relative links: fenced code blocks are ignored.
  $fence = [char]96 + [char]96 + [char]96
  $linkResult = Test-MarkdownRelativeLinks -Lines @($fence, '[ghost](docs/missing-example.md)', $fence, '[real](AGENTS.md)') -BaseDir $repo
  if ($linkResult.Checked -ne 1 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: fenced code block links should be ignored.'
  }
  $caseCount++

  # Markdown relative links: fragment-only resolution against an existing file.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('[baseline with anchor](docs/state/BASELINE.md#summary)') -BaseDir $repo
  if ($linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: fragment on existing file should resolve.'
  }
  $caseCount++

  # Markdown relative links: empty input is accepted.
  $linkResult = Test-MarkdownRelativeLinks -Lines @() -BaseDir $repo
  if ($linkResult.Checked -ne 0 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: empty input should produce no results.'
  }
  $caseCount++

  if ($caseCount -lt 12) {
    throw "docs-check self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "docs-check self-test passed ($caseCount cases; structural parser, multiline normalization, branch mode detection, BASELINE edit detection, live measurement, ADR frontmatter, Spec frontmatter, markdown relative links)."
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

# Exclude the navigation INDEX from ADR frontmatter validation; specs already exclude theirs via the SPEC-* filter.
$adrs = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/adr') -Filter '*.md' -File | Where-Object { $_.Name -ne 'INDEX.md' } | Sort-Object Name
foreach ($adr in $adrs) {
  $raw = Get-Content -LiteralPath $adr.FullName -Raw -Encoding UTF8
  try {
    $fields = ConvertFrom-AdrFrontmatter -RawText $raw
    Assert-AdrFrontmatter -Fields $fields -FileName $adr.Name
    if ($fields['id'] -notmatch '^ADR-[0-9]{4}$') { throw ($adr.Name + ' id must match ADR-NNNN pattern.') }
    if (-not $raw.Contains(('# ' + $fields['id']))) { throw ($adr.Name + ' heading does not contain the declared ID ' + $fields['id'] + '.') }
  } catch {
    throw ('ADR frontmatter validation failed for ' + $adr.Name + ': ' + $_.Exception.Message)
  }
}
$adrIds = @($adrs | ForEach-Object {
  $raw = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
  (ConvertFrom-AdrFrontmatter -RawText $raw)['id']
})
if (($adrIds | Sort-Object -Unique).Count -ne $adrIds.Count) { throw 'Duplicate ADR IDs detected.' }

$specIds = @()
foreach ($spec in ($specs | Sort-Object Name)) {
  $rawSpec = Get-Content -LiteralPath $spec.FullName -Raw -Encoding UTF8
  try {
    $specFields = ConvertFrom-SpecFrontmatter -RawText $rawSpec
    Assert-SpecFrontmatter -Fields $specFields -FileName $spec.Name
    if ($specFields['id'] -notmatch '^SPEC-[0-9]{4}$') { throw ($spec.Name + ' id must match SPEC-NNNN pattern.') }
    if (-not $rawSpec.Contains(('# ' + $specFields['id']))) { throw ($spec.Name + ' heading does not contain the declared ID ' + $specFields['id'] + '.') }
    # Cross-check id against filename stem (e.g. SPEC-0001-core-contracts.md -> SPEC-0001)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($spec.Name)
    $expectedId = ($stem -split '-')[0..1] -join '-'
    if ($expectedId -ne $specFields['id']) { throw ($spec.Name + ' filename stem ID mismatch: expected ' + $expectedId + ' but got ' + $specFields['id'] + '.') }
  } catch {
    throw ('Spec frontmatter validation failed for ' + $spec.Name + ': ' + $_.Exception.Message)
  }
  $specIds += $specFields['id']
}
if (($specIds | Sort-Object -Unique).Count -ne $specIds.Count) { throw 'Duplicate Spec IDs detected.' }

# Markdown relative links: every tracked markdown file must reference files
# that exist in the repository. External URLs, anchors, and fenced examples
# are intentionally out of scope.
$mdFiles = @(git -C $repo ls-files -- '*.md')
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked markdown files.' }
$linkCheckedTotal = 0
$brokenLinks = @()
foreach ($mdFile in $mdFiles) {
  $mdDir = Split-Path (Join-Path $repo $mdFile) -Parent
  $mdLines = Get-Content -LiteralPath (Join-Path $repo $mdFile)
  $result = Test-MarkdownRelativeLinks -Lines $mdLines -BaseDir $mdDir
  $linkCheckedTotal += $result.Checked
  foreach ($broken in $result.Broken) { $brokenLinks += ($mdFile + ' -> ' + $broken) }
}
if ($brokenLinks.Count -gt 0) {
  throw ('Broken relative markdown links detected: ' + ($brokenLinks -join '; '))
}

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

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; live measurement: {2} tracked paths, {3} repository JSON files, {4} markdown links.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count, $linkCheckedTotal)
