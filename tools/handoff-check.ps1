[CmdletBinding()]
param(
  [ValidateRange(-1, 2147483647)]
  [int]$Issue = -1,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$required = @(
  'AGENTS.md', 'README.md', 'docs/START_HERE.md', 'docs/AI_HANDOFF.md',
  'docs/PROJECT_MAP.md', 'docs/state/BASELINE.md', 'docs/tasks/active/0.md',
  'docs/tasks/active/TEMPLATE.md', 'docs/specs/INDEX.md',
  'docs/ai/HANDOFF_SCHEMA.json', 'docs/ai/MULTI_AGENT.md',
  'schemas/task-handoff-v1.schema.json', 'schemas/task-handoff-v2.schema.json'
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
  @{ Text = $agents; Token = 'docs/ai/MULTI_AGENT.md'; File = 'AGENTS.md' },
  @{ Text = $start; Token = 'docs/AI_HANDOFF.md'; File = 'docs/START_HERE.md' },
  @{ Text = $start; Token = 'docs/ai/MULTI_AGENT.md'; File = 'docs/START_HERE.md' },
  @{ Text = $summary; Token = '目前整合主線'; File = 'docs/AI_HANDOFF.md' },
  @{ Text = $summary; Token = '不可自行做的事'; File = 'docs/AI_HANDOFF.md' },
  @{ Text = $summary; Token = 'tools/context-pack.ps1'; File = 'docs/AI_HANDOFF.md' }
)) {
  if (-not $check.Text.Contains($check.Token)) {
    throw "Handoff anchor missing in $($check.File): $($check.Token)"
  }
}

function Get-FrontMatter([string]$text, [string]$path) {
  if (-not $text.StartsWith('---')) { throw "Active handoff must start with YAML front matter: $path" }
  $match = [regex]::Match($text, '(?s)^---\s*(?<body>.*?)\s*---')
  if (-not $match.Success -or [string]::IsNullOrWhiteSpace($match.Groups['body'].Value)) {
    throw "Active handoff front matter is missing: $path"
  }
  return $match.Groups['body'].Value
}

function Get-Scalar([string]$frontMatter, [string]$field, [string]$path) {
  $escaped = [regex]::Escape($field)
  $match = [regex]::Match($frontMatter, "(?m)^${escaped}:\s*(?<value>.+?)\s*$")
  if (-not $match.Success) { throw "Active handoff front matter is missing ${field}: $path" }
  $value = $match.Groups['value'].Value.Trim()
  if (($value.StartsWith('"') -and $value.EndsWith('"')) -or
      ($value.StartsWith("'") -and $value.EndsWith("'"))) {
    $value = $value.Substring(1, $value.Length - 2)
  }
  if ([string]::IsNullOrWhiteSpace($value)) { throw "Active handoff field is empty ${field}: $path" }
  return $value
}

function Get-InlineArray([string]$frontMatter, [string]$field, [string]$path) {
  $raw = Get-Scalar $frontMatter $field $path
  $match = [regex]::Match($raw, '^\[(?<body>.*)\]$')
  if (-not $match.Success) { throw "Active handoff ${field} must be an inline YAML array: $path" }
  $body = $match.Groups['body'].Value.Trim()
  if (-not $body) { return @() }
  $items = @($body -split ',' | ForEach-Object { $_.Trim().Trim('"', "'") })
  if (@($items | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count -gt 0) {
    throw "Active handoff ${field} contains an empty item: $path"
  }
  return $items
}

function Assert-UniqueItems([string[]]$items, [string]$field, [string]$path) {
  $unique = @($items | Sort-Object -Unique)
  if ($unique.Count -ne $items.Count) { throw "Active handoff ${field} contains duplicates: $path" }
}

function Assert-SafeBranch([string]$value, [string]$field, [string]$path) {
  if ($value -notmatch '^[A-Za-z0-9][A-Za-z0-9._/-]*$' -or
      $value.Contains('..') -or $value.Contains('//') -or $value.EndsWith('/')) {
    throw "Active handoff ${field} is not a safe Git ref: $value ($path)"
  }
}

function Assert-SafeScopePath([string]$value, [string]$field, [string]$path) {
  if ($value.StartsWith('/') -or $value.StartsWith('\') -or
      $value -match '^[A-Za-z]:' -or $value.Contains('\') -or
      $value -match '(^|/)\.\.(/|$)' -or $value -match '(^|/)\.local(/|$)') {
    throw "Active handoff ${field} must contain safe repository-relative paths: $value ($path)"
  }
}

function Test-GlobSegmentIntersection([string]$left, [string]$right) {
  $queue = [System.Collections.Generic.Queue[string]]::new()
  $visited = [System.Collections.Generic.HashSet[string]]::new()
  $queue.Enqueue('0:0')
  [void]$visited.Add('0:0')

  while ($queue.Count -gt 0) {
    $state = $queue.Dequeue().Split(':')
    $leftIndex = [int]$state[0]
    $rightIndex = [int]$state[1]
    if ($leftIndex -eq $left.Length -and $rightIndex -eq $right.Length) { return $true }
    if ($visited.Count -gt 4096) { return $true }

    if ($leftIndex -lt $left.Length -and $left[$leftIndex] -ceq '*') {
      $next = "$($leftIndex + 1):$rightIndex"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
    }
    if ($rightIndex -lt $right.Length -and $right[$rightIndex] -ceq '*') {
      $next = "$($leftIndex):$($rightIndex + 1)"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
    }
    if ($leftIndex -ge $left.Length -or $rightIndex -ge $right.Length) { continue }

    $leftToken = $left[$leftIndex]
    $rightToken = $right[$rightIndex]
    $compatible = $leftToken -ceq '*' -or $leftToken -ceq '?' -or
      $rightToken -ceq '*' -or $rightToken -ceq '?' -or
      $leftToken -ceq $rightToken
    if (-not $compatible) { continue }

    $nextLeft = if ($leftToken -ceq '*') { $leftIndex } else { $leftIndex + 1 }
    $nextRight = if ($rightToken -ceq '*') { $rightIndex } else { $rightIndex + 1 }
    $next = "$nextLeft`:$nextRight"
    if ($visited.Add($next)) { $queue.Enqueue($next) }
  }
  return $false
}

function Test-GlobIntersection([string]$left, [string]$right) {
  $leftSegments = @($left.Replace('\', '/') -split '/')
  $rightSegments = @($right.Replace('\', '/') -split '/')
  $queue = [System.Collections.Generic.Queue[string]]::new()
  $visited = [System.Collections.Generic.HashSet[string]]::new()
  $queue.Enqueue('0:0')
  [void]$visited.Add('0:0')

  while ($queue.Count -gt 0) {
    $state = $queue.Dequeue().Split(':')
    $leftIndex = [int]$state[0]
    $rightIndex = [int]$state[1]
    if ($leftIndex -eq $leftSegments.Count -and $rightIndex -eq $rightSegments.Count) { return $true }
    if ($visited.Count -gt 4096) { return $true }

    $leftAny = $leftIndex -lt $leftSegments.Count -and $leftSegments[$leftIndex] -ceq '**'
    $rightAny = $rightIndex -lt $rightSegments.Count -and $rightSegments[$rightIndex] -ceq '**'
    if ($leftAny) {
      $next = "$($leftIndex + 1):$rightIndex"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
    }
    if ($rightAny) {
      $next = "$($leftIndex):$($rightIndex + 1)"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
    }
    if ($leftIndex -ge $leftSegments.Count -or $rightIndex -ge $rightSegments.Count) { continue }

    if ($leftAny -and $rightAny) { continue }
    if ($leftAny) {
      $next = "$leftIndex`:$($rightIndex + 1)"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
      continue
    }
    if ($rightAny) {
      $next = "$($leftIndex + 1):$rightIndex"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
      continue
    }
    if (Test-GlobSegmentIntersection $leftSegments[$leftIndex] $rightSegments[$rightIndex]) {
      $next = "$($leftIndex + 1):$($rightIndex + 1)"
      if ($visited.Add($next)) { $queue.Enqueue($next) }
    }
  }
  return $false
}

function Test-HandoffFrontMatter {
  param(
    [Parameter(Mandatory)] [string]$Text,
    [Parameter(Mandatory)] [string]$Path,
    [string]$FileName = '',
    [AllowEmptyCollection()] [object[]]$SeenScopes = @(),
    $SeenBranches = $null
  )

  $frontMatter = Get-FrontMatter $Text $Path

  $schemaVersion = Get-Scalar $frontMatter 'schema_version' $Path
  if ($schemaVersion -ne '2') { throw "Active handoff must use schema_version 2: $Path" }

  $issueValueText = Get-Scalar $frontMatter 'issue' $Path
  $issueValue = 0
  if (-not [int]::TryParse($issueValueText, [ref]$issueValue) -or $issueValue -lt 0) {
    throw "Active handoff issue must be a non-negative integer: $Path"
  }
  if ($FileName -and $FileName -ne $issueValue.ToString()) {
    throw "Active handoff filename must match issue $issueValue`": $Path"
  }

  $branch = Get-Scalar $frontMatter 'branch' $Path
  $targetBranch = Get-Scalar $frontMatter 'target_branch' $Path
  Assert-SafeBranch $branch 'branch' $Path
  Assert-SafeBranch $targetBranch 'target_branch' $Path
  if ($branch -eq $targetBranch) { throw "Active handoff branch and target_branch must differ: $Path" }
  if ($SeenBranches -and $SeenBranches.ContainsKey($branch)) {
    throw "Active handoffs share branch '$branch': Issues $($SeenBranches[$branch]) and $issueValue"
  }
  if ($SeenBranches) { $SeenBranches[$branch] = $issueValue }

  $status = Get-Scalar $frontMatter 'status' $Path
  if ($status -notin @('planned', 'in_progress', 'blocked', 'ready_for_review')) {
    throw "Active handoff status is invalid: $status ($Path)"
  }
  $role = Get-Scalar $frontMatter 'role' $Path
  if ($role -notin @('worker', 'integrator')) { throw "Active handoff role is invalid: $role ($Path)" }
  [void](Get-Scalar $frontMatter 'owner' $Path)

  $updatedAt = Get-Scalar $frontMatter 'updated_at' $Path
  $parsedDate = [DateTimeOffset]::MinValue
  if (-not [DateTimeOffset]::TryParse($updatedAt, [ref]$parsedDate)) {
    throw "Active handoff updated_at is not an ISO date-time: $Path"
  }

  $base = Get-Scalar $frontMatter 'base_commit' $Path
  if ($base -notmatch '^[0-9a-fA-F]{7,40}$') { throw "Active handoff base_commit must be a Git SHA: $Path" }

  $scopeGlobs = @(Get-InlineArray $frontMatter 'scope_globs' $Path)
  if ($scopeGlobs.Count -lt 1 -or $scopeGlobs.Count -gt 32) {
    throw "Active handoff scope_globs must contain 1..32 items: $Path"
  }
  Assert-UniqueItems $scopeGlobs 'scope_globs' $Path
  foreach ($scope in $scopeGlobs) {
    Assert-SafeScopePath $scope 'scope_globs' $Path
    foreach ($previous in $SeenScopes) {
      if ($previous.Issue -ne $issueValue -and (Test-GlobIntersection $previous.Scope $scope)) {
        throw "Active handoff scopes overlap: Issue #$($previous.Issue) '$($previous.Scope)' and Issue #$issueValue '$scope' ($Path)"
      }
    }
  }

  $sharedPaths = @(Get-InlineArray $frontMatter 'shared_paths' $Path)
  if ($sharedPaths.Count -gt 32) { throw "Active handoff shared_paths exceeds 32 items: $Path" }
  Assert-UniqueItems $sharedPaths 'shared_paths' $Path
  foreach ($shared in $sharedPaths) { Assert-SafeScopePath $shared 'shared_paths' $Path }

  $dependencyTokens = @(Get-InlineArray $frontMatter 'depends_on' $Path)
  if ($dependencyTokens.Count -gt 16) { throw "Active handoff depends_on exceeds 16 items: $Path" }
  Assert-UniqueItems $dependencyTokens 'depends_on' $Path
  foreach ($token in $dependencyTokens) {
    $dependency = 0
    if (-not [int]::TryParse($token, [ref]$dependency) -or $dependency -lt 0) {
      throw "Active handoff depends_on must contain non-negative Issue IDs: $token ($Path)"
    }
    if ($dependency -eq $issueValue) { throw "Active handoff cannot depend on itself: $Path" }
  }

  [void](Get-Scalar $frontMatter 'next_safe_action' $Path)
  $resumeCommands = @(Get-InlineArray $frontMatter 'resume_commands' $Path)
  if ($resumeCommands.Count -lt 1 -or $resumeCommands.Count -gt 5) {
    throw "Active handoff resume_commands must contain 1..5 items: $Path"
  }

  foreach ($heading in @(
    '## Objective', '## Acceptance', '## Completed', '## Known limitations',
    '## Last verification', '## Remaining work', '## Next safe action', '## Resume commands'
  )) {
    if (-not $Text.Contains($heading)) { throw "Active handoff is missing heading ${heading}: $Path" }
  }

  $resumeMatch = [regex]::Match(
    $Text,
    '(?ms)^## Resume commands\s*$\s*```(?:powershell|pwsh)?\s*(?<body>.*?)\s*```')
  if (-not $resumeMatch.Success) { throw "Active handoff Resume commands needs a fenced command block: $Path" }
  $resumeLines = @($resumeMatch.Groups['body'].Value -split "`r?`n" |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
  if ($resumeLines.Count -lt 1 -or $resumeLines.Count -gt 5) {
    throw "Active handoff Resume commands block must contain 1..5 commands: $Path"
  }
  if ($resumeLines.Count -ne $resumeCommands.Count) {
    throw "Active handoff front matter and Resume commands block have different lengths: $Path"
  }
  for ($index = 0; $index -lt $resumeLines.Count; $index++) {
    if ($resumeLines[$index] -ne $resumeCommands[$index]) {
      throw "Active handoff front matter and Resume commands block differ at item $($index + 1): $Path"
    }
  }

  return @{ Issue = $issueValue; Branch = $branch; BaseCommit = $base }
}

if ($SelfTest) {
  $caseCount = 0
  $cases = @(
    @{ Left = 'src/**'; Right = 'src/hub/**'; Expected = $true },
    @{ Left = 'docs/tasks/active/*.md'; Right = 'docs/tasks/active/21.md'; Expected = $true },
    @{ Left = 'src/**/audio.cpp'; Right = 'src/hub/audio.cpp'; Expected = $true },
    @{ Left = 'src/hub/audio.cpp'; Right = 'src/hub/**'; Expected = $true },
    @{ Left = 'docs/tasks/active/0.md'; Right = 'docs/tasks/active/0.md'; Expected = $true },
    @{ Left = 'vst-host/**'; Right = 'apps/**'; Expected = $false },
    @{ Left = 'src/hub/**'; Right = 'src/core/**'; Expected = $false },
    @{ Left = 'tests/*.cpp'; Right = 'tests/*.hpp'; Expected = $false },
    @{ Left = 'src/**'; Right = 'src2/**'; Expected = $false }
  )
  foreach ($case in $cases) {
    $actual = Test-GlobIntersection $case.Left $case.Right
    if ($actual -ne $case.Expected) {
      throw "Handoff scope overlap self-test failed: '$($case.Left)' vs '$($case.Right)' expected $($case.Expected), got $actual."
    }
    $caseCount++
  }

  # Helper: build a minimal valid handoff fixture for pipeline tests.
  function New-ValidHandoff {
    param(
      [string]$IssueNumber = '99',
      [string]$Branch = 'codex/99-selftest',
      [string]$Status = 'planned',
      [string]$Role = 'worker',
      [string]$BaseCommit = 'abcdef1234567890abcdef1234567890abcdef12',
      [string]$ScopeGlobs = '["src/selftest/**"]',
      [string]$DependsOn = '[]',
      [string]$ResumeCommands = '["pwsh -File tools/handoff-check.ps1 -SelfTest"]',
      [string]$Body = ''
    )
    $head = @(
      '---',
      'schema_version: 2',
      "issue: $IssueNumber",
      "branch: $Branch",
      'target_branch: main',
      "base_commit: $BaseCommit",
      "status: $Status",
      "role: $Role",
      'owner: selftest-owner',
      'updated_at: 2026-08-22T00:00:00+08:00',
      "scope_globs: $ScopeGlobs",
      'shared_paths: []',
      "depends_on: $DependsOn",
      'next_safe_action: "Run the bounded self-test."',
      "resume_commands: $ResumeCommands"
      '---'
    ) -join "`n"
    $tail = @(
      '',
      '# Issue handoff self-test fixture',
      '',
      '## Objective',
      '',
      'Bounded in-memory self-test fixture.',
      '',
      '## Acceptance',
      '',
      '- Self-test passes.',
      '',
      '## Completed',
      '',
      '- Fixture generated.',
      '',
      '## Known limitations',
      '',
      '- In-memory only.',
      '',
      '## Last verification',
      '',
      '- Not executed.',
      '',
      '## Remaining work',
      '',
      '1. None.',
      '',
      '## Next safe action',
      '',
      'Run the bounded self-test.',
      '',
      '## Resume commands',
      '',
      '```powershell',
      'pwsh -File tools/handoff-check.ps1 -SelfTest',
      '```'
    ) -join "`n"
    return $head + $tail + $Body
  }

  # Helper: assert that a call throws (any rejection is a pass).
  function Assert-Throws {
    param([scriptblock]$Action, [string]$Label)
    $caught = $false
    try { & $Action } catch { $caught = $true }
    if (-not $caught) { throw "handoff-check self-test failed: expected rejection ($Label)." }
    $script:caseCount++
  }

  # Helper: assert that a call succeeds.
  function Assert-Passes {
    param([scriptblock]$Action, [string]$Label)
    try { & $Action | Out-Null } catch { throw "handoff-check self-test failed: unexpected rejection ($Label): $($_.Exception.Message)" }
    $script:caseCount++
  }

  # Pipeline case: valid fixture passes all front-matter checks.
  Assert-Passes { Test-HandoffFrontMatter -Text (New-ValidHandoff) -Path 'docs/tasks/active/99.md' -FileName '99' } 'valid fixture'

  # Missing front matter fails closed.
  Assert-Throws { Get-FrontMatter 'no front matter here' 'selftest' } 'missing front matter'

  # Scalar parsing: quoted and unquoted values round-trip; missing field rejects.
  $scalarFm = "field_one: plain-value`nfield_two: `"quoted value`"`nfield_three: 'single'"
  if ((Get-Scalar $scalarFm 'field_one' 'selftest') -ne 'plain-value') { throw 'handoff-check self-test failed: unquoted scalar.' }
  $script:caseCount++
  if ((Get-Scalar $scalarFm 'field_two' 'selftest') -ne 'quoted value') { throw 'handoff-check self-test failed: double-quoted scalar.' }
  $script:caseCount++
  if ((Get-Scalar $scalarFm 'field_three' 'selftest') -ne 'single') { throw 'handoff-check self-test failed: single-quoted scalar.' }
  $script:caseCount++
  Assert-Throws { Get-Scalar $scalarFm 'field_missing' 'selftest' } 'missing scalar'

  # Inline array parsing: valid arrays, empty array, and non-array rejection.
  $arrayFm = 'scope_globs: ["a/**", "b/**"]' + "`n" + 'shared_paths: []' + "`n" + 'bad_field: not-an-array'
  $parsedArray = @(Get-InlineArray $arrayFm 'scope_globs' 'selftest')
  if ($parsedArray.Count -ne 2 -or $parsedArray[0] -ne 'a/**' -or $parsedArray[1] -ne 'b/**') { throw 'handoff-check self-test failed: inline array parsing.' }
  $script:caseCount++
  $emptyArray = @(Get-InlineArray $arrayFm 'shared_paths' 'selftest')
  if ($emptyArray.Count -ne 0) { throw 'handoff-check self-test failed: empty inline array.' }
  $script:caseCount++
  Assert-Throws { Get-InlineArray $arrayFm 'bad_field' 'selftest' } 'non-array field'

  # Duplicate items in scope_globs fail closed.
  $dupFm = New-ValidHandoff -ScopeGlobs '["src/**", "src/**"]'
  Assert-Throws { Test-HandoffFrontMatter -Text $dupFm -Path 'docs/tasks/active/99.md' -FileName '99' } 'duplicate scope globs'

  # schema_version mismatch fails closed.
  $badSchema = (New-ValidHandoff).Replace('schema_version: 2', 'schema_version: 1')
  Assert-Throws { Test-HandoffFrontMatter -Text $badSchema -Path 'docs/tasks/active/99.md' -FileName '99' } 'wrong schema version'

  # Issue/filename mismatch fails closed.
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff) -Path 'docs/tasks/active/99.md' -FileName '100' } 'issue filename mismatch'

  # Negative issue number fails closed.
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff -IssueNumber '-1') -Path 'docs/tasks/active/99.md' -FileName '99' } 'negative issue'

  # Unsafe branch names are rejected.
  foreach ($unsafeBranch in @('../escape', 'branch//double', 'ends-with/', '.hidden')) {
    Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff -Branch $unsafeBranch) -Path 'docs/tasks/active/99.md' -FileName '99' } "unsafe branch: $unsafeBranch"
  }

  # branch == target_branch fails closed.
  $sameBranchText = (New-ValidHandoff).Replace("branch: codex/99-selftest", "branch: main")
  Assert-Throws { Test-HandoffFrontMatter -Text $sameBranchText -Path 'docs/tasks/active/99.md' -FileName '99' } 'branch equals target'

  # Invalid status enum fails closed.
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff -Status 'done') -Path 'docs/tasks/active/99.md' -FileName '99' } 'invalid status'

  # Invalid role enum fails closed.
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff -Role 'admin') -Path 'docs/tasks/active/99.md' -FileName '99' } 'invalid role'

  # Malformed base_commit SHA fails closed.
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff -BaseCommit 'not-a-sha!') -Path 'docs/tasks/active/99.md' -FileName '99' } 'malformed sha'

  # Scope overlap between two different issues fails closed.
  $seenScopes = @(
    [pscustomobject]@{ Issue = 98; Scope = 'src/selftest/**' }
  )
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff) -Path 'docs/tasks/active/99.md' -FileName '99' -SeenScopes $seenScopes } 'scope overlap'

  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff) -Path 'docs/tasks/active/99.md' -FileName '99' -SeenScopes $seenScopes } 'scope overlap'

  # depends_on self-reference fails closed.
  $selfDepFm = New-ValidHandoff -DependsOn '["99"]'
  Assert-Throws { Test-HandoffFrontMatter -Text $selfDepFm -Path 'docs/tasks/active/99.md' -FileName '99' } 'self dependency'

  # depends_on non-integer fails closed.
  $badDepFm = New-ValidHandoff -DependsOn '["not-a-number"]'
  Assert-Throws { Test-HandoffFrontMatter -Text $badDepFm -Path 'docs/tasks/active/99.md' -FileName '99' } 'non-integer dependency'

  # Missing required heading fails closed (remove ## Objective).
  $missingHeading = (New-ValidHandoff).Replace('## Objective', '## Different')
  Assert-Throws { Test-HandoffFrontMatter -Text $missingHeading -Path 'docs/tasks/active/99.md' -FileName '99' } 'missing heading'

  # Resume commands mismatch between front matter and fenced block fails closed.
  $resumeMismatch = New-ValidHandoff -ResumeCommands '["command-one", "command-two"]'
  Assert-Throws { Test-HandoffFrontMatter -Text $resumeMismatch -Path 'docs/tasks/active/99.md' -FileName '99' } 'resume command count mismatch'

  # Duplicate branch across handoffs fails closed via SeenBranches.
  $seenBranches = @{ "codex/99-selftest" = 98 }
  Assert-Throws { Test-HandoffFrontMatter -Text (New-ValidHandoff) -Path 'docs/tasks/active/99.md' -FileName '99' -SeenBranches $seenBranches } 'duplicate branch'

  if ($caseCount -lt 25) {
    throw "handoff-check self-test failed: expected at least 25 passing cases, saw $caseCount."
  }
  Write-Output "handoff-check self-test passed ($caseCount cases; glob overlap, pipeline parsing, validation and resume consistency)."
  exit 0
}

$activeRoot = Join-Path $repo 'docs/tasks/active'
if ($Issue -ge 0) {
  $selectedPath = Join-Path $activeRoot "$Issue.md"
  if (-not (Test-Path -LiteralPath $selectedPath)) { throw "No active handoff exists for Issue $Issue." }
  $handoffFiles = @(Get-Item -LiteralPath $selectedPath)
} else {
  $handoffFiles = @(Get-ChildItem -LiteralPath $activeRoot -Filter '*.md' -File |
    Where-Object { $_.BaseName -match '^\d+$' } | Sort-Object { [int]$_.BaseName })
}
if ($handoffFiles.Count -eq 0) { throw 'No numeric active handoffs were found.' }

$branchContext = $env:GITHUB_HEAD_REF
if (-not $branchContext) { $branchContext = (& git -C $repo branch --show-current).Trim() }
if (-not $branchContext) { $branchContext = $env:GITHUB_REF_NAME }

$seenBranches = @{}
$seenScopes = [System.Collections.Generic.List[object]]::new()
$checked = @()
foreach ($file in $handoffFiles) {
  $path = [IO.Path]::GetRelativePath($repo, $file.FullName).Replace('\', '/')
  $handoff = Get-Content -LiteralPath $file.FullName -Raw
  $result = Test-HandoffFrontMatter -Text $handoff -Path $path -FileName $file.BaseName -SeenScopes $seenScopes.ToArray() -SeenBranches $seenBranches

  $issueValue = $result.Issue
  $branch = $result.Branch
  $base = $result.BaseCommit
  & git -C $repo cat-file -e "$base^{commit}"
  if ($LASTEXITCODE -ne 0) { throw "Active handoff base_commit is not present locally: $base ($path)" }

  $mustCheckAncestry = ($Issue -ge 0) -or ($branchContext -and $branchContext -eq $branch)
  if ($mustCheckAncestry) {
    if ($branchContext -and $branchContext -ne $branch) {
      throw "Selected handoff branch '$branch' does not match current branch '$branchContext': $path"
    }
    & git -C $repo merge-base --is-ancestor $base HEAD
    if ($LASTEXITCODE -ne 0) { throw "Active handoff base_commit is not an ancestor of HEAD: $base ($path)" }
  }

  $checked += "#$issueValue@$branch"
}

Write-Output "AI handoff checks passed ($($checked -join ', '); schema v2 ownership, scope and Git state)."
