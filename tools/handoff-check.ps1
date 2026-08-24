#Requires -Version 7
[CmdletBinding()]
param(
  [ValidateRange(-1, 2147483647)]
  [int]$Issue = -1,
  [switch]$AdmissionPrecheck,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

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

function Register-IssueBranch {
  param(
    [Parameter(Mandatory)] [hashtable]$SeenBranches,
    [Parameter(Mandatory)] [string]$Branch,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Path
  )
  if ($SeenBranches.ContainsKey($Branch)) {
    throw "Issues share branch '$Branch': $($SeenBranches[$Branch]) and $IssueNumber"
  }
  $SeenBranches[$Branch] = $IssueNumber
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
  $lifecycleLabels = @($Labels | Where-Object { $_ -in @('claim-pending', 'claimed', 'in-review', 'done') })
  if ($lifecycleLabels.Count -ne 1) {
    throw "Issue #$IssueNumber must have exactly one of claimed/in-review/done, got: $($lifecycleLabels -join ', ') ($Path)"
  }
  if ($lifecycleLabels[0] -eq 'done') {
    throw "Issue #$IssueNumber has done label but is still $State; close it instead ($Path)"
  }
  return $lifecycleLabels[0]
}

function Get-AuditedLifecycleLabel {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Labels,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$State,
    [Parameter(Mandatory)] [string]$Path,
    [Parameter(Mandatory)] [scriptblock]$IssueLookup,
    [ValidateRange(1, 10000)][int]$RetryDelayMilliseconds = 3000
  )
  $lifecycleLabels = @($Labels | Where-Object { $_ -in @('claim-pending', 'claimed', 'in-review', 'done') })
  if ($lifecycleLabels.Count -eq 1) {
    return $lifecycleLabels[0]
  }
  if ($lifecycleLabels.Count -gt 1) {
    throw "Issue #$IssueNumber must have exactly one of claim-pending/claimed/in-review/done, got: $($lifecycleLabels -join ', ') ($Path)"
  }
  # Admission swaps remove the old lifecycle label before adding the new one.
  # A global audit racing that window sees zero labels for one snapshot; re-read
  # the single issue a bounded number of times instead of failing immediately.
  # Persistent violations (still zero or still multiple labels) fail closed.
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    Start-Sleep -Milliseconds ($RetryDelayMilliseconds * $attempt)
    $fresh = & $IssueLookup $IssueNumber
    if (-not $fresh) { break }
    $freshLabels = @($fresh.labels | ForEach-Object { $_.name })
    $freshLifecycle = @($freshLabels | Where-Object { $_ -in @('claim-pending', 'claimed', 'in-review', 'done') })
    if ($freshLifecycle.Count -gt 1) { break }
    if ($freshLifecycle.Count -eq 1) {
      Write-Host ("Issue #{0} showed a transient lifecycle state during the global audit; re-read confirmed '{1}'." -f $IssueNumber, $freshLifecycle[0])
      return $freshLifecycle[0]
    }
  }
  throw "Issue #$IssueNumber must have exactly one lifecycle label but none was present after bounded re-reads ($Path)"
}

function Test-IssueRequiresHandoff {
  param([Parameter(Mandatory)] $IssueData)
  $labelNames = @($IssueData.labels | ForEach-Object { $_.name })
  $hasLifecycle = @($labelNames | Where-Object { $_ -in @('claim-pending', 'claimed', 'in-review', 'done') }).Count -gt 0
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
  # Issue bodies can arrive as CRLF. Normalize before line-anchored matching;
  # otherwise the end anchor sees CR before LF and cannot match line ends.
  $normalizedBody = $Body -replace "\r\n", "`n"
  $match = [regex]::Match($normalizedBody, '(?s)<!-- hibiki:handoff-v1\s*(?<body>.*?)\s*-->')
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

function Get-HandoffScalar {
  param(
    [Parameter(Mandatory)] [string]$Body,
    [Parameter(Mandatory)] [string]$Key
  )
  # Keep admission and audit parsing identical for CRLF issue bodies.
  $normalizedBody = $Body -replace "\r\n", "`n"
  $match = [regex]::Match($normalizedBody, '(?s)<!-- hibiki:handoff-v1\s*(?<body>.*?)\s*-->')
  if (-not $match.Success) { throw "Missing hibiki:handoff-v1 block while reading '$Key'." }
  $line = [regex]::Match($match.Groups['body'].Value, "(?im)^\s*$([regex]::Escape($Key))\s*:\s*(?<value>[^\r\n]+)$")
  if (-not $line.Success) { throw "Handoff block is missing required key '$Key'." }
  $value = $line.Groups['value'].Value.Trim()
  if (($value.StartsWith('"') -and $value.EndsWith('"') -and $value.Length -ge 2) -or
      ($value.StartsWith("'") -and $value.EndsWith("'") -and $value.Length -ge 2)) {
    $value = $value.Substring(1, $value.Length - 2)
  }
  if ([string]::IsNullOrWhiteSpace($value)) { throw "Handoff key '$Key' is empty." }
  return $value
}

function Get-AdmissionIssueNumber {
  param(
    [Parameter(Mandatory)] [string]$Body,
    [Parameter(Mandatory)] [string]$Key
  )
  return [int]::Parse((Get-HandoffScalar -Body $Body -Key $Key))
}

function Get-HandoffInlineArray {
  param(
    [Parameter(Mandatory)] [string]$Body,
    [Parameter(Mandatory)] [string]$Key
  )
  $scalar = (Get-HandoffScalar -Body $Body -Key $Key).Trim()
  if ($scalar -eq '[]') { return @() }
  try { $parsed = $scalar | ConvertFrom-Json } catch { throw "Handoff key '$Key' must be a JSON array." }
  if ($parsed -is [System.Array]) { return @($parsed | ForEach-Object { [string]$_ }) }
  return @([string]$parsed)
}

function Get-AdmissionIssues {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$OpenIssues,
    [Parameter(Mandatory)] [int]$SelectedIssueNumber
  )
  $issues = [System.Collections.Generic.List[object]]::new()
  foreach ($candidate in $OpenIssues) {
    if (-not ([string]$candidate.body).Contains('hibiki:handoff-v1')) { continue }
    if (@(Get-TbdHandoffFields -Body ([string]$candidate.body)).Count -ne 0) { continue }
    [void]$issues.Add($candidate)
  }
  if ($issues.Count -ge 500) { throw 'Open handoff Issue count reached admission cap 500; fail closed.' }
  $selected = [System.Collections.Generic.List[object]]::new()
  foreach ($item in $issues) {
    if ([int]$item.number -eq [int]$SelectedIssueNumber) { [void]$selected.Add($item) }
  }
  if ($selected.Count -ne 1) { throw "Selected Issue #$SelectedIssueNumber does not have a complete handoff block." }
  return @($selected[0])
}

function Assert-AdmissionOverlap {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$OpenIssues,
    [Parameter(Mandatory)] [int]$SelectedIssueNumber
  )
  function New-ScopeRecord {
    param([int]$IssueNumber, [string]$Scope)
    [pscustomobject]@{ Issue = $IssueNumber; Scope = $Scope }
  }
  $seenScopes = [System.Collections.Generic.List[object]]::new()
  $seenBranches = @{}
  foreach ($issue in $OpenIssues) {
    $path = "issue/$($issue.number)"
    $branch = Get-HandoffScalar -Body $issue.body -Key 'branch'
    Assert-SafeBranch -Value $branch -Key 'branch' -Path $path
    Register-IssueBranch -SeenBranches $seenBranches -Branch $branch -IssueNumber $issue.number -Path $path
    $scopeGlobs = @(Get-HandoffInlineArray -Body $issue.body -Key 'scope_globs')
    if ($scopeGlobs.Count -lt 1 -or $scopeGlobs.Count -gt 32) {
      throw "Issue handoff scope_globs must contain 1..32 items: $path"
    }
    Assert-UniqueItems -Values ([string[]]$scopeGlobs) -Key 'scope_globs' -Path $path
    foreach ($scope in $scopeGlobs) {
      Assert-SafeScopePath -Value $scope -Key 'scope_globs' -Path $path
      foreach ($previous in $seenScopes) {
        if ($previous.Issue -ne $issue.number -and (Test-GlobIntersection -Left $previous.Scope -Right $scope)) {
          throw "Scope overlap: Issue #$($previous.Issue) '$($previous.Scope)' and Issue #$($issue.number) '$scope'"
        }
      }
      [void]$seenScopes.Add((New-ScopeRecord -IssueNumber $issue.number -Scope $scope))
    }
  }
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

function Test-PendingNoAssigneeReadback {
  param(
    [Parameter(Mandatory)] [psobject]$Readback,
    [Parameter(Mandatory)] [int]$IssueNumber
  )
  $labels = @($Readback.labels | ForEach-Object { $_.name })
  $owners = @($Readback.assignees | ForEach-Object { $_.login })
  if ([string]$Readback.state -ne 'OPEN') { throw "Issue #$IssueNumber readback is not open." }
  $forbiddenPending = @($labels | Where-Object { $_ -in @('claimed', 'in-review', 'done') })
  if ($labels -notcontains 'claim-pending' -or $forbiddenPending.Count -ne 0) {
    throw "Issue #$IssueNumber claim-pending label readback mismatch."
  }
  if ($owners.Count -ne 0) {
    throw "Issue #$IssueNumber claim-pending must not have assignee(s): $($owners -join ', ')."
  }
}

function Test-ClaimedAdmissionReadback {
  param(
    [Parameter(Mandatory)] [psobject]$Readback,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Owner
  )
  $labels = @($Readback.labels | ForEach-Object { $_.name })
  $owners = @($Readback.assignees | ForEach-Object { $_.login })
  if ([string]$Readback.state -ne 'OPEN') { throw "Issue #$IssueNumber claimed readback is not open." }
  $forbiddenClaimed = @($labels | Where-Object { $_ -in @('claim-pending', 'in-review', 'done') })
  if ($labels -notcontains 'claimed' -or $forbiddenClaimed.Count -ne 0) {
    throw "Issue #$IssueNumber claimed label readback mismatch."
  }
  if ($owners.Count -ne 1 -or $owners[0] -ne $Owner) {
    throw "Issue #$IssueNumber owner claimed readback mismatch."
  }
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

  $seenSelfTestBranches = @{}
  Register-IssueBranch -SeenBranches $seenSelfTestBranches `
    -Branch 'codex/99-selftest' -IssueNumber 99 -Path 'selftest/branch-first'
  if ($seenSelfTestBranches['codex/99-selftest'] -ne 99) {
    throw 'handoff-check self-test failed: branch registration was not recorded.'
  }
  Assert-Throws {
    Register-IssueBranch -SeenBranches $seenSelfTestBranches `
      -Branch 'codex/99-selftest' -IssueNumber 100 -Path 'selftest/shared-branch'
  } 'two issues sharing one branch'
  $distinctSelfTestBranches = @{}
  foreach ($case in @(@{ Branch = 'codex/98-first'; Issue = 98 }, @{ Branch = 'codex/97-second'; Issue = 97 })) {
    Register-IssueBranch -SeenBranches $distinctSelfTestBranches `
      -Branch $case.Branch -IssueNumber $case.Issue -Path 'selftest/distinct-branches'
  }
  if ($distinctSelfTestBranches.Count -ne 2) {
    throw 'handoff-check self-test failed: distinct branches were rejected.'
  }
  $caseCount++

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

  $unclaimedDraft = New-MockIssue -Number 100 -Branch 'codex/100-unclaimed-draft' -Labels @() -Assignees @()
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
  $crlfTbdBody = ($tbdDraftBody -replace "`n", "`r`n")
  $crlfTbdFields = @(Get-TbdHandoffFields -Body $crlfTbdBody)
  if ($crlfTbdFields.Count -ne 2 -or $crlfTbdFields -notcontains "issue" -or $crlfTbdFields -notcontains "branch") {
    throw "handoff-check self-test failed: CRLF TBD draft fields were not detected."
  }
  $caseCount++
  if (@(Get-TbdHandoffFields -Body $mock.body).Count -ne 0) {
    throw "handoff-check self-test failed: filled handoff was treated as TBD draft."
  }
  $caseCount++

  # Admission helper coverage: exact handoff parsing, selected-issue lookup,
  # branch uniqueness, and scope overlap use the same fail-closed semantics.
  if ((Get-HandoffScalar -Body $mock.body -Key 'branch') -ne 'codex/99-selftest') {
    throw 'handoff-check self-test failed: admission scalar parser.'
  }
  $caseCount++
  # Convert the LF-only mock body to CRLF without relying on escape parsing.
  $crlfMockBody = $mock.body.Replace([string][char]10, [string][char]13 + [string][char]10)
  if ((Get-HandoffScalar -Body $crlfMockBody -Key 'branch') -ne 'codex/99-selftest') {
    throw 'handoff-check self-test failed: CRLF admission scalar parser.'
  }
  $caseCount++
  if ((Get-AdmissionIssueNumber -Body $mock.body -Key 'issue') -ne 99) {
    throw 'handoff-check self-test failed: admission issue parser.'
  }
  Assert-Throws { Get-HandoffScalar -Body $mock.body -Key 'missing' } 'admission missing key'
  $scopes = @(Get-HandoffInlineArray -Body $mock.body -Key 'scope_globs')
  if ($scopes.Count -ne 1 -or $scopes[0] -ne 'src/selftest/**') {
    throw 'handoff-check self-test failed: admission array parser.'
  }
  $caseCount++

  $selected = @(Get-AdmissionIssues -OpenIssues @($mock, $unclaimedDraft) -SelectedIssueNumber 99)
  $selectedArray = @($selected)
  $selectedIssue = $null
  foreach ($candidate in $selectedArray) {
    if ([int]$candidate.number -eq 99) { $selectedIssue = $candidate; break }
  }
  if ($null -eq $selectedIssue -or [int]$selectedIssue.number -ne 99) {
    throw 'handoff-check self-test failed: selected admission issue.'
  }
  $caseCount++
  Assert-Throws {
    Get-AdmissionIssues -OpenIssues @($mock) -SelectedIssueNumber 100
  } 'admission selected issue missing'
  Assert-Throws { Assert-SafeBranch '../escape' 'branch' 'selftest/admission' } 'admission unsafe branch'

  $leftIssue = New-MockIssue -Number 101 -Branch 'codex/101-left' -ScopeGlobs '["src/shared/**"]'
  $rightIssue = New-MockIssue -Number 102 -Branch 'codex/102-right' -ScopeGlobs '["src/other/**"]'
  Assert-AdmissionOverlap -OpenIssues @($leftIssue, $rightIssue) -SelectedIssueNumber 102 | Out-Null
  $caseCount++
  $overlapIssue = New-MockIssue -Number 103 -Branch 'codex/103-overlap' -ScopeGlobs '["src/shared/audio/**"]'
  Assert-Throws {
    Assert-AdmissionOverlap -OpenIssues @($leftIssue, $overlapIssue) -SelectedIssueNumber 103
  } 'admission scope overlap'
  $sameBranch = New-MockIssue -Number 104 -Branch 'codex/101-left' -ScopeGlobs '["src/independent/**"]'
  Assert-Throws {
    Assert-AdmissionOverlap -OpenIssues @($leftIssue, $sameBranch) -SelectedIssueNumber 104
  } 'admission duplicate branch'

  # claim-pending with claimed must fail closed (two lifecycle labels)
  Assert-Throws {
    Assert-LifecycleLabel -Labels @('claim-pending', 'claimed') -IssueNumber 99 -State 'OPEN' -Path 'selftest/pending-plus-claimed'
  } 'claim-pending plus claimed'

  # claim-pending alone is a valid single lifecycle label (non-authoritative marker)
  $pendingResult = Assert-LifecycleLabel -Labels @('claim-pending') -IssueNumber 99 -State 'OPEN' -Path 'selftest/pending-only'
  if ($pendingResult -ne 'claim-pending') { throw "handoff-check self-test failed: pending-only returned '$pendingResult'." }
  $caseCount++

  # Global audit re-read: transient zero-label snapshot recovers to claimed.
  $script:selfTestIssueReads = 0
  $recovered = Get-AuditedLifecycleLabel -Labels @() -IssueNumber 77 -State 'OPEN' -Path 'selftest/audit-transient-recovery' `
    -RetryDelayMilliseconds 1 -IssueLookup {
      param([int]$n)
      $script:selfTestIssueReads++
      return @{ labels = @(@{ name = 'claimed' }) }
    }
  if ($recovered -ne 'claimed') { throw "handoff-check self-test failed: transient recovery returned '$recovered'." }
  $caseCount++

  # Global audit re-read: persistent zero-label state must still fail closed.
  Assert-Throws {
    Get-AuditedLifecycleLabel -Labels @() -IssueNumber 78 -State 'OPEN' -Path 'selftest/audit-transient-persistent' `
      -RetryDelayMilliseconds 1 -IssueLookup { param([int]$n) return @{ labels = @() } }
  } 'audit persistent lifecycle violation'
  $caseCount++

  # Global audit re-read: two labels stay a hard violation without retry.
  $script:selfTestTwoLabelReads = 0
  Assert-Throws {
    Get-AuditedLifecycleLabel -Labels @('claimed', 'in-review') -IssueNumber 79 -State 'OPEN' -Path 'selftest/audit-two-labels' `
      -RetryDelayMilliseconds 1 -IssueLookup {
        param([int]$n)
        $script:selfTestTwoLabelReads++
        return @{ labels = @(@{ name = 'claimed' }, @{ name = 'in-review' }) }
      }
  } 'audit duplicate lifecycle violation'
  if ($script:selfTestTwoLabelReads -ne 0) { throw 'handoff-check self-test failed: duplicate lifecycle labels should not trigger re-read.' }
  $caseCount++

  # Admission readbacks exercise the real helper functions (SPEC-0004 semantics).
  $pendingPass = @{ state = 'OPEN'; labels = @(@{ name = 'claim-pending' }); assignees = @() }
  Test-PendingNoAssigneeReadback -Readback $pendingPass -IssueNumber 99
  $caseCount++

  Assert-Throws {
    Test-PendingNoAssigneeReadback -Readback @{
      state = 'OPEN'; labels = @(@{ name = 'claim-pending' }); assignees = @(@{ login = 'someone' })
    } -IssueNumber 99
  } 'pending readback with assignee'
  $closedPending = @{ state = 'CLOSED'; labels = @(@{ name = 'claim-pending' }); assignees = @() }
  Assert-Throws { Test-PendingNoAssigneeReadback -Readback $closedPending -IssueNumber 99 } 'pending readback closed issue'

  Test-ClaimedAdmissionReadback -Readback @{
    state = 'OPEN'; labels = @(@{ name = 'claimed' }); assignees = @(@{ login = 'owner1' })
  } -IssueNumber 99 -Owner 'owner1'
  $caseCount++

  foreach ($forbiddenExtra in @('claim-pending', 'in-review', 'done')) {
    Assert-Throws {
      Test-ClaimedAdmissionReadback -Readback @{
        state = 'OPEN'
        labels = @(@{ name = 'claimed' }, @{ name = $forbiddenExtra })
        assignees = @(@{ login = 'owner1' })
      } -IssueNumber 99 -Owner 'owner1'
    } "claimed readback with extra '$forbiddenExtra'"
  }

  Assert-Throws {
    Test-ClaimedAdmissionReadback -Readback @{
      state = 'OPEN'; labels = @(@{ name = 'claimed' }); assignees = @(@{ login = 'wrong' })
    } -IssueNumber 99 -Owner 'owner1'
  } 'claimed readback wrong owner'

  Assert-Throws {
    Test-ClaimedAdmissionReadback -Readback @{
      state = 'OPEN'; labels = @(@{ name = 'claimed' }); assignees = @()
    } -IssueNumber 99 -Owner 'owner1'
  } 'claimed readback zero owners'

  if ($caseCount -lt 5) { throw "handoff-check self-test failed: expected at least 5 passing cases, saw $caseCount." }
  Write-Output "handoff-check self-test passed (issue-block parsing, TBD draft skip, state/labels, owner mismatch, glob overlap, safe paths and arrays)."
  exit 0
}

function Get-AdmissionIssueData {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$OpenIssues
  )
  $selected = [System.Collections.Generic.List[object]]::new()
  foreach ($candidate in $OpenIssues) {
    if ([int]$candidate.number -eq $IssueNumber) { [void]$selected.Add($candidate) }
  }
  if ($selected.Count -ne 1) { throw "Expected exactly one Issue #$IssueNumber, found $($selected.Count)." }
  return $selected[0]
}

function Test-AdmissionIssueEligibility {
  param([Parameter(Mandatory)] [psobject]$IssueData)
  if ([string]$IssueData.state -ne 'OPEN') { throw "Issue #$($IssueData.number) is not open." }
  $labelNames = @($IssueData.labels | ForEach-Object { $_.name })
  foreach ($forbiddenLabel in @('claim-pending', 'claimed', 'in-review', 'done')) {
    if ($labelNames -contains $forbiddenLabel) {
      throw "Issue #$($IssueData.number) already has lifecycle label '$forbiddenLabel'."
    }
  }
  if (@($IssueData.assignees).Count -gt 0) {
    throw "Issue #$($IssueData.number) already has an assignee."
  }
}

function Get-AdmissionOwner {
  param([Parameter(Mandatory)] [string]$Body, [Parameter(Mandatory)] [int]$IssueNumber)
  $owner = Get-HandoffScalar -Body $Body -Key 'owner'
  if ($owner -eq 'TBD') { throw "Issue #$IssueNumber owner is still TBD." }
  if ($owner -match '\s') { throw "Issue #$IssueNumber owner is not a single account name." }
  return $owner
}

function Get-AdmissionBaseCommit {
  param([Parameter(Mandatory)] [string]$Body, [Parameter(Mandatory)] [int]$IssueNumber)
  $base = Get-HandoffScalar -Body $Body -Key 'base_commit'
  if ($base -notmatch '^[0-9a-fA-F]{40}$') { throw "Issue #$IssueNumber base_commit is not a full Git SHA." }
  return $base.ToLowerInvariant()
}

function Add-IssueLabelSafe {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Label
  )
  & gh label create $Label --color D4C5F9 2>$null | Out-Null
  if ($LASTEXITCODE -notin @(0, 1)) { throw "Unable to ensure lifecycle label '$Label' exists." }
  & gh issue edit $IssueNumber --add-label $Label
  if ($LASTEXITCODE -ne 0) { throw "Unable to add lifecycle label '$Label' to Issue #$IssueNumber." }
}

function ConvertTo-AdmissionReadback {
  param([Parameter(Mandatory)] [string[]]$GhArgs)
  $json = & gh @GhArgs 2>&1
  if ($LASTEXITCODE -ne 0) { throw "Issue readback failed: $json" }
  return ($json | ConvertFrom-Json)
}

function Set-IssueLabelsSafe {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string[]]$Add,
    [Parameter(Mandatory)] [string[]]$Remove
  )
  foreach ($label in $Add) { Add-IssueLabelSafe -IssueNumber $IssueNumber -Label $label }
  foreach ($label in $Remove) {
    & gh issue edit $IssueNumber --remove-label $label
    if ($LASTEXITCODE -ne 0) { throw "Unable to remove label '$label' from Issue #$IssueNumber." }
  }
}

function Remove-IssueAdmissionStateSafe {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Owner,
    [Parameter(Mandatory)] [string]$Branch,
    [Parameter(Mandatory)] [string]$OwnerRepo
  )
  & gh issue edit $IssueNumber --remove-label 'claim-pending' 2>$null | Out-Null
  & gh issue edit $IssueNumber --remove-label 'claimed' 2>$null | Out-Null
  & gh issue edit $IssueNumber --remove-assignee $Owner 2>$null | Out-Null
  & gh api --method DELETE ('/repos/' + $OwnerRepo + '/git/refs/heads/' + $Branch) 2>$null | Out-Null
}

if ($AdmissionPrecheck) {
  if ($Issue -lt 0) { throw '-AdmissionPrecheck requires -Issue.' }
  $maxOpenIssues = 500
  $ghArgs = @('issue', 'list', '--state', 'open', '--limit', "$maxOpenIssues",
    '--json', 'number,state,title,body,labels,assignees')
  $issuesJson = & gh @ghArgs 2>&1
  if ($LASTEXITCODE -ne 0) { throw "gh issue list failed: $issuesJson" }
  $openIssues = @($issuesJson | ConvertFrom-Json)
  if ($openIssues.Count -ge $maxOpenIssues) {
    throw "Open Issue count reached cap $maxOpenIssues; raise the explicit limit rather than silently truncating."
  }
  $selectedIssues = @(Get-AdmissionIssues -OpenIssues $openIssues -SelectedIssueNumber $Issue)
  if ($selectedIssues.Count -ne 1 -or [int]$selectedIssues[0].number -ne $Issue) {
    throw "Selected Issue #$Issue does not have exactly one complete admission handoff."
  }
  $selectedIssue = $selectedIssues[0]
  if ($selectedIssue.state -ine 'OPEN') { throw "Selected Issue #$Issue is not open." }
  $labels = @($selectedIssue.labels | ForEach-Object { $_.name })
  foreach ($forbiddenLabel in @('claim-pending', 'claimed', 'in-review', 'done')) {
    if ($labels -contains $forbiddenLabel) {
      throw "Selected Issue #$Issue already has lifecycle label '$forbiddenLabel'; cannot admit."
    }
  }
  if (@($selectedIssue.assignees).Count -gt 0) {
    throw "Selected Issue #$Issue already has an assignee; cannot admit."
  }
  $normalizedTitles = @{}
  foreach ($candidate in $openIssues) {
    $titleKey = ([string]$candidate.title).Trim().ToLowerInvariant()
    if (-not $normalizedTitles.ContainsKey($titleKey)) {
      $normalizedTitles[$titleKey] = [System.Collections.Generic.List[int]]::new()
    }
    [void]$normalizedTitles[$titleKey].Add([int]$candidate.number)
  }
  $selectedTitleKey = ([string]$selectedIssue.title).Trim().ToLowerInvariant()
  if ($normalizedTitles[$selectedTitleKey].Count -gt 1) {
    $others = @($normalizedTitles[$selectedTitleKey] | Where-Object { $_ -ne $Issue })
    throw "Normalized exact-title conflict for Issue #$Issue with Issue(s): $($others -join ', ')"
  }
  Assert-AdmissionOverlap -OpenIssues $openIssues -SelectedIssueNumber $Issue | Out-Null
  Write-Output "claim-admission preflight passed for Issue #$Issue ($($openIssues.Count) open issues scanned)."
  exit 0
}
# Main path: enumerate open issues and validate their hibiki:handoff-v1 blocks.
$maxOpenIssues = 500
$ghArgs = @('issue', 'list', '--state', 'open', '--limit', "$maxOpenIssues",
  '--json', 'number,state,title,body,labels,assignees')
$issuesJson = & gh @ghArgs 2>&1
if ($LASTEXITCODE -ne 0) { throw "gh issue list failed: $issuesJson" }
$issues = $issuesJson | ConvertFrom-Json
if (@($issues).Count -ge $maxOpenIssues) {
  throw "Open Issue count reached cap $maxOpenIssues; raise the explicit limit rather than silently truncating."
}

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
    $labelNamesForSkip = @($issueData.labels | ForEach-Object { $_.name })
    $hasLifecycleLabel = @($labelNamesForSkip | Where-Object { $_ -in @('claim-pending', 'claimed', 'in-review') }).Count -gt 0
    if ($hasLifecycleLabel) {
      throw "Issue #$issueNumber has lifecycle label but still contains TBD in handoff block; fail closed. ($path)"
    }
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
  Register-IssueBranch -SeenBranches $seenBranches -Branch $branch `
    -IssueNumber $issueNumber -Path $path
  $labels = @($issueData.labels | ForEach-Object { $_.name })
  if ($Issue -ge 0) {
    Assert-LifecycleLabel -Labels $labels -IssueNumber $issueNumber -State $issueData.state -Path $path
  } else {
    Get-AuditedLifecycleLabel -Labels $labels -IssueNumber $issueNumber -State $issueData.state -Path $path `
      -IssueLookup { param([int]$n) Get-IssueHandoff -IssueNumber $n } | Out-Null
  }
  $hasPendingLabel = $labels -contains 'claim-pending'
  if ($hasPendingLabel) {
    $assigneeLogins = @($issueData.assignees | ForEach-Object { $_.login })
    if ($assigneeLogins.Count -gt 0) {
      throw "Issue #$issueNumber has claim-pending label but already has assignee(s): $($assigneeLogins -join ', ') ($path)"
    }
    # claim-pending is a non-authoritative admission marker; it must not have
    # an assignee yet (SPEC-0004). Owner assignment is validated only after
    # the admission workflow atomically swaps pending -> claimed.
    $ownerField = Get-Scalar $frontMatter 'owner' $path
    [void]$ownerField
  } else {
    $ownerField = Get-Scalar $frontMatter 'owner' $path
    $assigneeLogins = @($issueData.assignees | ForEach-Object { $_.login })
    Assert-OwnerAssignment -Owner $ownerField -AssigneeLogins $assigneeLogins -IssueNumber $issueNumber -Path $path
  }

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
