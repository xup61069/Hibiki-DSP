[CmdletBinding()]
param(
  [ValidateRange(-1, 2147483647)]
  [int]$Issue = -1,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$repo = Split-Path -Parent $PSScriptRoot

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


function Get-FrontMatter {
  param(
    [Parameter(Mandatory)] [string]$Block,
    [Parameter(Mandatory)] [string]$Path
  )
  $lines = @($Block -split "\r?\n")
  if ($lines.Count -lt 3 -or $lines[0] -ne '---' -or $lines[-1] -ne '---') {
    throw "Issue handoff block must contain YAML front matter: $Path"
  }
  $frontMatter = @{}
  foreach ($rawLine in $lines[1..($lines.Count - 2)]) {
    $line = $rawLine.Trim()
    if (-not $line -or $line.StartsWith('#')) { continue }
    $separator = $line.IndexOf(':')
    if ($separator -lt 1) { throw "Invalid handoff line '$line': $Path" }
    $key = $line.Substring(0, $separator).Trim()
    $value = $line.Substring($separator + 1).Trim()
    if ($frontMatter.ContainsKey($key)) { throw "Duplicate handoff key '$key': $Path" }
    $frontMatter[$key] = $value
  }
  return $frontMatter
}

function Get-Scalar {
  param(
    [Parameter(Mandatory)] [hashtable]$FrontMatter,
    [Parameter(Mandatory)] [string]$Key,
    [Parameter(Mandatory)] [string]$Path
  )
  if (-not $FrontMatter.ContainsKey($Key)) { throw "Issue handoff is missing required key '$Key': $Path" }
  $value = $FrontMatter[$Key].Trim()
  if (($value.StartsWith('"') -and $value.EndsWith('"') -and $value.Length -ge 2) -or
      ($value.StartsWith("'") -and $value.EndsWith("'") -and $value.Length -ge 2)) {
    $value = $value.Substring(1, $value.Length - 2)
  }
  if ([string]::IsNullOrWhiteSpace($value)) { throw "Issue handoff key '$Key' is empty: $Path" }
  return $value
}

function Get-InlineArray {
  param(
    [Parameter(Mandatory)] [hashtable]$FrontMatter,
    [Parameter(Mandatory)] [string]$Key,
    [Parameter(Mandatory)] [string]$Path
  )
  $scalar = Get-Scalar -FrontMatter $FrontMatter -Key $Key -Path $Path
  if ($scalar -eq '[]') { return @() }
  try { $parsed = $scalar | ConvertFrom-Json } catch { throw "Issue handoff key '$Key' must be a JSON array: $Path" }
  if ($parsed -is [System.Array]) { return @($parsed | ForEach-Object { [string]$_ }) }
  return @([string]$parsed)
}

function Assert-SafeBranch {
  param([string]$Value, [string]$Key, [string]$Path)
  if ($Value -notmatch '^[A-Za-z0-9][A-Za-z0-9._/-]*[A-Za-z0-9]$' -or
      $Value.Contains('//') -or $Value.Contains('..') -or $Value.Contains('--')) {
    throw "Issue handoff $Key contains an unsafe Git branch name '$Value': $Path"
  }
}

function Assert-SafeScopePath {
  param([string]$Value, [string]$Key, [string]$Path)
  $normalized = $Value.Replace('\', '/')
  if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized.StartsWith('/') -or
      $normalized -match '^[A-Za-z]:' -or $normalized.Split('/') -contains '..' -or
      $normalized.StartsWith('.local/') -or $normalized -eq '.local') {
    throw "Issue handoff $Key contains an unsafe path or forbidden area '$Value': $Path"
  }
}

function Assert-UniqueItems {
  param([AllowEmptyCollection()] [string[]]$Values, [string]$Key, [string]$Path)
  $distinct = @($Values | Select-Object -Unique)
  if ($distinct.Count -ne $Values.Count) {
    throw "Issue handoff $Key contains duplicate items: $Path"
  }
}

function Assert-LifecycleLabel {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Labels,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$State,
    [Parameter(Mandatory)] [string]$Path
  )
  $lifecycleLabels = @($Labels | Where-Object { $_ -in @('claimed', 'in-review', 'done') })
  if ($lifecycleLabels.Count -ne 1) {
    throw "Issue #$IssueNumber must have exactly one of claimed/in-review/done, got: $($lifecycleLabels -join ', ') ($Path)"
  }
  if ($lifecycleLabels[0] -eq 'done') {
    throw "Issue #$IssueNumber has done label but is still $State; close it instead ($Path)"
  }
}

function Test-IssueRequiresHandoff {
  param([Parameter(Mandatory)] $IssueData)
  $labelNames = @($IssueData.labels | ForEach-Object { $_.name })
  $hasLifecycle = @($labelNames | Where-Object { $_ -in @('claimed', 'in-review', 'done') }).Count -gt 0
  $hasAssignee = @($IssueData.assignees).Count -gt 0
  return $hasLifecycle -or $hasAssignee
}

function Assert-OwnerAssignment {
  param(
    [Parameter(Mandatory)] [string]$Owner,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$AssigneeLogins,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Path
  )
  if ($AssigneeLogins.Count -ne 1) {
    throw "Issue #$IssueNumber must have exactly one assignee matching owner '$Owner', got $($AssigneeLogins.Count): $Path"
  }
  if ($AssigneeLogins[0] -ne $Owner) {
    throw "Issue #$IssueNumber owner '$Owner' does not match assignee '$($AssigneeLogins[0])': $Path"
  }
}

function Get-IssueHandoff {
  param([Parameter(Mandatory)] [int]$IssueNumber)
  $json = & gh issue view $IssueNumber --json number,state,title,body,labels,assignees 2>&1
  if ($LASTEXITCODE -ne 0) { throw "Issue #$IssueNumber could not be read via gh: $json" }
  return $json | ConvertFrom-Json


}

function Get-BodyBlock {
  param(
    [Parameter(Mandatory)] [string]$Body,
    [Parameter(Mandatory)] [string]$Path
  )
  if (-not $Body.Contains('<!-- hibiki:handoff-v1')) { throw "Issue body is missing the hibiki:handoff-v1 block: $Path" }
  $match = [regex]::Match($Body, '(?s)<!-- hibiki:handoff-v1\s*(?<body>.*?)\s*-->')
  if (-not $match.Success -or [string]::IsNullOrWhiteSpace($match.Groups['body'].Value)) {
    throw "Issue body hibiki:handoff-v1 block is empty or unterminated: $Path"
  }
  return ("---`n" + $match.Groups['body'].Value + "`n---")
}

function Get-TbdHandoffFields {
  param(
    [Parameter(Mandatory)] [string]$Body
  )
  $match = [regex]::Match($Body, '(?s)<!-- hibiki:handoff-v1\s*(?<body>.*?)\s*-->')
  if (-not $match.Success) { return @() }
  $fields = [System.Collections.Generic.List[string]]::new()
  foreach ($key in @('issue', 'branch')) {
    $line = [regex]::Match($match.Groups['body'].Value, "(?im)^\s*$key\s*:\s*(?<value>[^\r\n]+)$")
    if (-not $line.Success) { continue }
    $value = $line.Groups['value'].Value.Trim()
    if (($value.StartsWith('"') -and $value.EndsWith('"') -and $value.Length -ge 2) -or
        ($value.StartsWith("'") -and $value.EndsWith("'") -and $value.Length -ge 2)) {
      $value = $value.Substring(1, $value.Length - 2)
    }
    if ($value.Contains('TBD')) { [void]$fields.Add($key) }
  }
  return @($fields)
}

function Test-IssueState {
  param(
    [Parameter(Mandatory)] $IssueData,
    [Parameter(Mandatory)] [string]$ExpectedBranch,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$RequiredLabels,
    [Parameter(Mandatory)] [string]$Path
  )
  if ($IssueData.state -eq 'closed') { throw "Issue is closed; no active claim allowed: $($IssueData.number) ($Path)" }
  $labelNames = @($IssueData.labels | ForEach-Object { $_.name })
  foreach ($requiredLabel in $RequiredLabels) {
    if ($labelNames -notcontains $requiredLabel) {
      throw "Issue is missing required label '$requiredLabel': #$($IssueData.number) ($Path)"
    }
  }
  return $labelNames
}

# Main validation path

# Helper: assert that a call throws (any rejection is a pass).
function Assert-Throws {
  param([scriptblock]$Action, [string]$Label)
  $caught = $false
  try { & $Action } catch { $caught = $true }
  if (-not $caught) { throw "handoff-check self-test failed: expected rejection ($Label)." }
}
function Resolve-BranchContext {
  param(
    [AllowNull()][string]$HeadRef,
    [AllowNull()][string]$CurrentBranch,
    [AllowNull()][string]$RefName
  )

  foreach ($candidate in @($HeadRef, $CurrentBranch, $RefName)) {
    if (-not [string]::IsNullOrWhiteSpace($candidate)) { return $candidate.Trim() }
  }
  return $null
}

if ($SelfTest) {
  $caseCount = 0
  $branchContextCases = @(
    @{ HeadRef = 'codex/from-head-ref'; CurrentBranch = 'codex/from-branch'; RefName = 'main'; Expected = 'codex/from-head-ref' },
    @{ HeadRef = ''; CurrentBranch = ' codex/from-branch '; RefName = 'main'; Expected = 'codex/from-branch' },
    @{ HeadRef = ''; CurrentBranch = ''; RefName = ' main '; Expected = 'main' },
    @{ HeadRef = ''; CurrentBranch = ''; RefName = ''; Expected = $null }
  )
  foreach ($case in $branchContextCases) {
    $actual = Resolve-BranchContext $case.HeadRef $case.CurrentBranch $case.RefName
    if ($actual -ne $case.Expected) {
      throw "Branch context self-test failed: expected '$($case.Expected)', got '$actual'."
    }
    $caseCount++
  }


  function New-MockIssue {
    param(
      [int]$Number = 99,
      [string]$State = 'OPEN',
      [string]$Branch = 'codex/99-selftest',
      [string]$ScopeGlobs = '["src/selftest/**"]',
      [string[]]$Labels = @('claimed'),
      [string[]]$Assignees = @('selftest-owner'),
      [string]$Owner = 'selftest-owner'
    )
    $labelText = ($Labels | ForEach-Object { '"' + $_ + '"' }) -join ', '
    $blockLines = @(
      '<!-- hibiki:handoff-v1',
      'schema_version: 2',
      "issue: $Number",
      "branch: $Branch",
      'target_branch: main',
      "base_commit: abcdef1234567890abcdef1234567890abcdef12",
      ("labels: [" + $labelText + "]"),
      "scope_globs: $ScopeGlobs",
      'shared_paths: []',
      'depends_on: []',
      ('owner: "' + $Owner + '"'),
      'next_safe_action: "Run the bounded self-test."',
      'resume_commands: ["pwsh -File tools/handoff-check.ps1 -SelfTest"]',
      '-->'
    )
    return @{
      number = $Number
      state = $State
      title = "Self-test issue $Number"
      labels = @($Labels | ForEach-Object { @{ name = $_ } })
      assignees = @($Assignees | ForEach-Object { @{ login = $_ } })
      body = "# Objective`n`nTest objective.`n`n" + ($blockLines -join "`n")
    }
  }
  $mock = New-MockIssue
  $block = Get-BodyBlock -Body $mock.body -Path 'selftest/99'
  $frontMatter = Get-FrontMatter $block 'selftest/99'
  if ((Get-Scalar $frontMatter 'branch' 'selftest/99') -ne 'codex/99-selftest') { throw 'self-test failed: branch parse.' }
  $caseCount++

  Assert-Throws { Get-BodyBlock -Body 'no block here' 'selftest/none' } 'missing handoff block'
  Assert-Throws { Get-BodyBlock -Body '<!-- hibiki:handoff-v1' 'selftest/unterm' } 'unterminated handoff block'

  $closedMock = New-MockIssue -State 'CLOSED'
  Assert-Throws { Test-IssueState -IssueData $closedMock -ExpectedBranch 'codex/99-selftest' -RequiredLabels @('claimed') -Path 'selftest/closed' } 'closed issue'

  $noLabelMock = New-MockIssue -Labels @()
  Assert-Throws { Test-IssueState -IssueData $noLabelMock -ExpectedBranch 'codex/99-selftest' -RequiredLabels @('claimed') -Path 'selftest/nolabel' } 'missing claimed label'

  if (Test-GlobIntersection 'src/a/**' 'src/b/**') { throw 'self-test failed: disjoint globs wrongly overlap.' }
  $caseCount++
  if (-not (Test-GlobIntersection 'src/**' 'src/hub/audio.cpp')) { throw 'self-test failed: overlapping globs not detected.' }
  $caseCount++

  foreach ($unsafe in @('../escape', 'branch//double', 'ends-with/', '.hidden')) {
    Assert-Throws { Assert-SafeBranch $unsafe 'branch' 'selftest' } "unsafe branch: $unsafe"
  }

  foreach ($unsafePath in @('/abs/path', 'C:\path', '..\escape', '.local/data')) {
    Assert-Throws { Assert-SafeScopePath $unsafePath 'scope_globs' 'selftest' } "unsafe scope: $unsafePath"
  }

  Assert-Throws { Assert-UniqueItems @('a/**', 'a/**') 'scope_globs' 'selftest' } 'duplicate scopes'

  $twoLabelsMock = New-MockIssue -Labels @('claimed', 'in-review')
  Assert-Throws {
    $labelNames = @($twoLabelsMock.labels | ForEach-Object { $_.name })
    Assert-LifecycleLabel -Labels $labelNames -IssueNumber $twoLabelsMock.number -State $twoLabelsMock.state -Path 'selftest/two-labels'
  } 'two lifecycle labels'

  $doneOpenMock = New-MockIssue -Labels @('done')
  Assert-Throws {
    $labelNames = @($doneOpenMock.labels | ForEach-Object { $_.name })
    Assert-LifecycleLabel -Labels $labelNames -IssueNumber $doneOpenMock.number -State $doneOpenMock.state -Path 'selftest/open-done'
  } 'open done label'
  $caseCount++

  $ownerMismatchMock = New-MockIssue -Owner 'declared-owner' -Assignees @('actual-owner')
  Assert-Throws {
    $assigneeNames = @($ownerMismatchMock.assignees | ForEach-Object { $_.login })
    Assert-OwnerAssignment -Owner 'declared-owner' -AssigneeLogins $assigneeNames -IssueNumber 99 -Path 'selftest/owner-mismatch'
  } 'owner mismatch'
  $ownerMissingMock = New-MockIssue -Owner 'declared-owner' -Assignees @()
  Assert-Throws {
    $assigneeNames = @($ownerMissingMock.assignees | ForEach-Object { $_.login })
    Assert-OwnerAssignment -Owner 'declared-owner' -AssigneeLogins $assigneeNames -IssueNumber 99 -Path 'selftest/owner-missing'
  } 'missing assignee'
  $caseCount++

  $unclaimedDraft = New-MockIssue -Labels @() -Assignees @()
  if (Test-IssueRequiresHandoff $unclaimedDraft) {
    throw 'handoff-check self-test failed: unassigned unlabeled draft requires a handoff.'
  }
  $claimedWithoutBlock = New-MockIssue -Labels @('claimed') -Assignees @()
  if (-not (Test-IssueRequiresHandoff $claimedWithoutBlock)) {
    throw 'handoff-check self-test failed: claimed Issue did not require a handoff.'
  }
  $assignedWithoutBlock = New-MockIssue -Labels @() -Assignees @('selftest-owner')
  if (-not (Test-IssueRequiresHandoff $assignedWithoutBlock)) {
    throw 'handoff-check self-test failed: assigned Issue did not require a handoff.'
  }
  $caseCount++

  $tbdBlockLines = @(
    '<!-- hibiki:handoff-v1',
    'schema_version: 2',
    'issue: TBD',
    'branch: codex/TBD-placeholder',
    'target_branch: main',
    '-->'
  )
  $tbdDraftBody = "# Objective`n`n" + ($tbdBlockLines -join "`n")
  $tbdFields = @(Get-TbdHandoffFields -Body $tbdDraftBody)
  if ($tbdFields.Count -ne 2 -or $tbdFields -notcontains 'issue' -or $tbdFields -notcontains 'branch') {
    throw "handoff-check self-test failed: TBD draft fields were not detected."
  }
  $caseCount++
  if (@(Get-TbdHandoffFields -Body $mock.body).Count -ne 0) {
    throw "handoff-check self-test failed: filled handoff was treated as TBD draft."
  }
  $caseCount++

  if ($caseCount -lt 5) { throw "handoff-check self-test failed: expected at least 5 passing cases, saw $caseCount." }
  Write-Output "handoff-check self-test passed (issue-block parsing, TBD draft skip, state/labels, owner mismatch, glob overlap, safe paths and arrays)."
  exit 0
}
# Main path: enumerate open issues and validate their hibiki:handoff-v1 blocks.
$ghArgs = @('issue', 'list', '--state', 'open', '--limit', '200',
  '--json', 'number,state,title,body,labels,assignees')
$issuesJson = & gh @ghArgs 2>&1
if ($LASTEXITCODE -ne 0) { throw "gh issue list failed: $issuesJson" }
$issues = $issuesJson | ConvertFrom-Json

$withHandoff = @($issues | Where-Object { $_.body -match 'hibiki:handoff-v1' })
$withoutRequiredHandoff = @($issues | Where-Object {
  $_.body -notmatch 'hibiki:handoff-v1' -and (Test-IssueRequiresHandoff $_)
})

if ($Issue -lt 0 -and $withoutRequiredHandoff.Count -gt 0) {
  $entries = @($withoutRequiredHandoff | ForEach-Object {
    $labels = @($_.labels | ForEach-Object { $_.name }) -join ', '
    $assignees = @($_.assignees | ForEach-Object { $_.login }) -join ', '
    "#$($_.number) (labels=[$labels], assignees=[$assignees])"
  })
  throw "Assigned or lifecycle-labeled open Issue(s) are missing hibiki:handoff-v1: $($entries -join '; ')"
}

if ($withHandoff.Count -eq 0) {
  if ($Issue -ge 0) { throw "No active handoff block exists for Issue $Issue." }
  Write-Output 'AI handoff checks passed (0 open issues carry a hibiki:handoff-v1 block; nothing to validate).'
  exit 0
}
$seenBranches = @{}
$seenScopes = [System.Collections.Generic.List[object]]::new()
$checked = @()
$skippedTbdDrafts = [System.Collections.Generic.List[string]]::new()
foreach ($issueData in $withHandoff) {
  $issueNumber = $issueData.number
  if ($Issue -ge 0 -and $issueNumber -ne $Issue) { continue }
  $path = "issue/$issueNumber"

  $tbdFields = @(Get-TbdHandoffFields -Body $issueData.body)
  if ($tbdFields.Count -gt 0) {
    [void]$skippedTbdDrafts.Add("$path ($($tbdFields -join ', '))")
    continue
  }

  $block = Get-BodyBlock -Body $issueData.body -Path $path
  $frontMatter = Get-FrontMatter $block $path
    $schemaVersion = Get-Scalar $frontMatter 'schema_version' $path
  if ($schemaVersion -ne '2') { throw "Issue handoff must use schema_version 2: $path (got $schemaVersion)" }

  $issueField = Get-Scalar $frontMatter 'issue' $path
  $parsedIssue = 0
  if (-not [int]::TryParse($issueField, [ref]$parsedIssue) -or $parsedIssue -ne $issueNumber) {
    throw "Issue handoff issue field must match the actual issue number: expected $issueNumber, got '$issueField' ($path)"
  }
  $branch = Get-Scalar $frontMatter 'branch' $path
  $targetBranch = Get-Scalar $frontMatter 'target_branch' $path
  Assert-SafeBranch $branch 'branch' $path
  Assert-SafeBranch $targetBranch 'target_branch' $path
  if ($branch -eq $targetBranch) { throw "Issue handoff branch and target_branch must differ: $path" }
  if ($seenBranches.ContainsKey($branch)) {
    throw "Issues share branch '$branch': $($seenBranches[$branch]) and $issueNumber"
  }
  $seenBranches[$branch] = $issueNumber
  $labels = @($issueData.labels | ForEach-Object { $_.name })
  Assert-LifecycleLabel -Labels $labels -IssueNumber $issueNumber -State $issueData.state -Path $path
  $ownerField = Get-Scalar $frontMatter 'owner' $path
  $assigneeLogins = @($issueData.assignees | ForEach-Object { $_.login })
  Assert-OwnerAssignment -Owner $ownerField -AssigneeLogins $assigneeLogins -IssueNumber $issueNumber -Path $path

  # Label protocol is authoritative; front matter must not carry a conflicting status.
  if ($frontMatter.ContainsKey('status')) {
    throw "Issue handoff must not contain legacy status key; use claimed/in-review/done labels: $path"
  }

  $scopeGlobs = @(Get-InlineArray $frontMatter 'scope_globs' $path)
  if ($scopeGlobs.Count -lt 1 -or $scopeGlobs.Count -gt 32) {
    throw "Issue handoff scope_globs must contain 1..32 items: $path"
  }
  Assert-UniqueItems $scopeGlobs 'scope_globs' $path
  foreach ($scope in $scopeGlobs) {
    Assert-SafeScopePath $scope 'scope_globs' $path
    if ($scope -match '(?i)\bTBD\b') { throw "Issue handoff scope_globs contains unresolved TBD path '$scope': $path" }
    foreach ($previous in $seenScopes) {
      if ($previous.Issue -ne $issueNumber -and (Test-GlobIntersection $previous.Scope $scope)) {
        throw "Issue handoff scopes overlap: Issue #$($previous.Issue) '$($previous.Scope)' and Issue #$issueNumber '$scope' ($path)"
      }
    }
  }
  foreach ($s in $scopeGlobs) { $seenScopes.Add(@{ Issue = $issueNumber; Scope = $s }) }

  $sharedPaths = @(Get-InlineArray $frontMatter 'shared_paths' $path)
  if ($sharedPaths.Count -gt 32) { throw "Issue handoff shared_paths exceeds 32 items: $path" }
  Assert-UniqueItems $sharedPaths 'shared_paths' $path
  foreach ($shared in $sharedPaths) { Assert-SafeScopePath $shared 'shared_paths' $path }

  [void](Get-InlineArray $frontMatter 'depends_on' $path)

  $base = Get-Scalar $frontMatter 'base_commit' $path
  if ($base -notmatch '^[0-9a-fA-F]{7,40}$') { throw "Issue handoff base_commit must be a Git SHA: $base ($path)" }
  & git -C $repo cat-file -e "$base^{commit}" 2>$null
  if ($LASTEXITCODE -ne 0) { throw "Issue handoff base_commit is not present locally: $base ($path)" }

  if ($Issue -ge 0 -and $issueNumber -eq $Issue) {
    $currentBranch = (& git -C $repo branch --show-current 2>$null | Select-Object -First 1)
    $branchContext = Resolve-BranchContext $env:GITHUB_HEAD_REF $currentBranch $env:GITHUB_REF_NAME
    if ($branchContext -and $branchContext -ne $branch) {
      throw "Selected issue branch '$branch' does not match current branch '$branchContext': $path"
    }
    & git -C $repo merge-base --is-ancestor $base HEAD
    if ($LASTEXITCODE -ne 0) { throw "Issue handoff base_commit is not an ancestor of HEAD: $base ($path)" }
  }

  [void](Get-Scalar $frontMatter 'next_safe_action' $path)
  $resumeCommands = @(Get-InlineArray $frontMatter 'resume_commands' $path)
  if ($resumeCommands.Count -lt 1 -or $resumeCommands.Count -gt 5) {
    throw "Issue handoff resume_commands must contain 1..5 items: $path"
  }

  $checked += "#$issueNumber@$branch"
}

if ($skippedTbdDrafts.Count -gt 0) {
  $warningEntries = @($skippedTbdDrafts | Select-Object -First 8)
  if ($skippedTbdDrafts.Count -gt $warningEntries.Count) {
    $warningEntries += "(+ $($skippedTbdDrafts.Count - $warningEntries.Count) more)"
  }
  Write-Warning ("Skipping pre-claim draft handoff(s): " + ($warningEntries -join ', '))
}

$issueWasSkipped = $false
if ($Issue -ge 0) {
  $issueWasSkipped = @($skippedTbdDrafts | Where-Object { $_.StartsWith("issue/$Issue ") }).Count -gt 0
}
if ($Issue -ge 0 -and -not ($checked | Where-Object { $_.StartsWith("#$Issue@") }) -and -not $issueWasSkipped) {
  throw "No active handoff block exists for Issue $Issue (or it is closed)."
}

Write-Output "AI handoff checks passed ($($checked -join ', '); issue-based ownership, scope and Git state)."
