#Requires -Version 7
[CmdletBinding()]
param(
  [ValidateRange(-1, 2147483647)]
  [int]$Issue = -1,
  [switch]$BeforeExplore,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$script:HandoffMarker = 'hibiki:handoff-v1'
$script:ExecutionRequestMarker = 'hibiki:execution-request-v1'
$script:LifecycleLabels = @('claim-pending', 'claimed', 'in-review', 'done')
$script:AllowedCheckConclusions = @('SUCCESS', 'NEUTRAL', 'SKIPPED')

function Get-ObjectPropertyValue {
  param(
    [AllowNull()]$InputObject,
    [Parameter(Mandatory)] [string]$Name,
    [AllowNull()]$Default = $null
  )
  if ($null -eq $InputObject) { return $Default }
  if ($InputObject -is [System.Collections.IDictionary]) {
    if ($InputObject.Contains($Name)) { return $InputObject[$Name] }
    return $Default
  }
  $property = $InputObject.PSObject.Properties[$Name]
  if ($null -eq $property) { return $Default }
  return $property.Value
}

function Get-HandoffBlockText {
  param(
    [Parameter(Mandatory)] [string]$Body,
    [Parameter(Mandatory)] [string]$Path
  )
  $normalized = $Body -replace "`r`n", "`n"
  $match = [regex]::Match(
    $normalized,
    '(?ms)^[ \t]*<!-- hibiki:handoff-v1[ \t]*\n(?<body>.*?)\n[ \t]*-->[ \t]*$'
  )
  if (-not $match.Success -or [string]::IsNullOrWhiteSpace($match.Groups['body'].Value)) {
    throw "Open execution Issue has a missing, empty, or unterminated handoff block: $Path"
  }
  return $match.Groups['body'].Value
}

function Get-HandoffScalar {
  param(
    [Parameter(Mandatory)] [string]$Block,
    [Parameter(Mandatory)] [string]$Key,
    [Parameter(Mandatory)] [string]$Path
  )
  $line = [regex]::Match(
    $Block,
    "(?im)^\s*$([regex]::Escape($Key))\s*:\s*(?<value>[^\r\n]+)$"
  )
  if (-not $line.Success) { throw "Handoff block is missing required key '$Key': $Path" }
  $value = $line.Groups['value'].Value.Trim()
  if (($value.StartsWith('"') -and $value.EndsWith('"') -and $value.Length -ge 2) -or
      ($value.StartsWith("'") -and $value.EndsWith("'") -and $value.Length -ge 2)) {
    $value = $value.Substring(1, $value.Length - 2)
  }
  if ([string]::IsNullOrWhiteSpace($value)) { throw "Handoff key '$Key' is empty: $Path" }
  return $value
}

function Get-UnresolvedHandoffKeys {
  param([Parameter(Mandatory)] [string]$Block)
  $keys = [System.Collections.Generic.List[string]]::new()
  foreach ($rawLine in @($Block -split "\r?\n")) {
    $line = [regex]::Match($rawLine, '^\s*(?<key>[A-Za-z_][A-Za-z0-9_-]*)\s*:\s*(?<value>.*)$')
    if (-not $line.Success) { continue }
    $key = $line.Groups['key'].Value
    $value = $line.Groups['value'].Value.Trim()
    $hasTbd = [regex]::IsMatch($value, '(?i)(?<![A-Za-z0-9_])TBD(?![A-Za-z0-9_])')
    $hasUnassignedOwner = $key -ieq 'owner' -and $value.Trim('"', "'") -ieq 'unassigned'
    if (($hasTbd -or $hasUnassignedOwner) -and -not $keys.Contains($key)) {
      [void]$keys.Add($key)
    }
  }
  return @($keys)
}

function Get-LifecycleLabel {
  param(
    [Parameter(Mandatory)]$IssueData,
    [Parameter(Mandatory)] [string]$Path
  )
  $labels = @((Get-ObjectPropertyValue -InputObject $IssueData -Name 'labels' -Default @()) |
    ForEach-Object { [string](Get-ObjectPropertyValue -InputObject $_ -Name 'name' -Default '') })
  $lifecycle = @($labels | Where-Object { $_ -in $script:LifecycleLabels })
  if ($lifecycle.Count -ne 1) {
    throw "Open handoff Issue must have exactly one lifecycle label; got [$($lifecycle -join ', ')]: $Path"
  }
  if ($lifecycle[0] -eq 'done') {
    throw "Open handoff Issue uses lifecycle 'done'; close the Issue instead: $Path"
  }
  return $lifecycle[0]
}

function Get-IssueLabelNames {
  param([Parameter(Mandatory)]$IssueData)
  return @((Get-ObjectPropertyValue -InputObject $IssueData -Name 'labels' -Default @()) |
    ForEach-Object { [string](Get-ObjectPropertyValue -InputObject $_ -Name 'name' -Default '') })
}

function Get-ClosingIssueNumbers {
  param([Parameter(Mandatory)]$PullRequest)
  return @((Get-ObjectPropertyValue -InputObject $PullRequest -Name 'closingIssuesReferences' -Default @()) |
    ForEach-Object { [int](Get-ObjectPropertyValue -InputObject $_ -Name 'number' -Default 0) })
}

function Get-CurrentHeadCheckState {
  param([Parameter(Mandatory)]$PullRequest)
  $checks = @((Get-ObjectPropertyValue -InputObject $PullRequest -Name 'statusCheckRollup' -Default @()))
  $allSettledAllowed = $checks.Count -gt 0
  $hasVerify = $false
  $hasCodeQlAnalyze = $false

  foreach ($check in $checks) {
    $defaultName = Get-ObjectPropertyValue -InputObject $check -Name 'context' -Default ''
    $name = [string](Get-ObjectPropertyValue -InputObject $check -Name 'name' -Default $defaultName)
    $workflow = [string](Get-ObjectPropertyValue -InputObject $check -Name 'workflowName' -Default '')
    $status = [string](Get-ObjectPropertyValue -InputObject $check -Name 'status' -Default '')
    $conclusion = [string](Get-ObjectPropertyValue -InputObject $check -Name 'conclusion' -Default '')
    $state = [string](Get-ObjectPropertyValue -InputObject $check -Name 'state' -Default '')

    if ($name -ieq 'verify') { $hasVerify = $true }
    if ($workflow -ieq 'CodeQL' -and $name -match '(?i)^Analyze(?:\s|\()') {
      $hasCodeQlAnalyze = $true
    }

    $entryAllowed = $false
    if (-not [string]::IsNullOrWhiteSpace($status) -or -not [string]::IsNullOrWhiteSpace($conclusion)) {
      $entryAllowed = $status -ieq 'COMPLETED' -and
        $conclusion.ToUpperInvariant() -in $script:AllowedCheckConclusions
    }
    elseif (-not [string]::IsNullOrWhiteSpace($state)) {
      $entryAllowed = $state -ieq 'SUCCESS'
    }
    if (-not $entryAllowed) { $allSettledAllowed = $false }
  }

  return [pscustomobject]@{
    Count = $checks.Count
    HasVerify = $hasVerify
    HasCodeQlAnalyze = $hasCodeQlAnalyze
    AllSettledAllowed = $allSettledAllowed
    IsGreen = $allSettledAllowed -and $hasVerify -and $hasCodeQlAnalyze
  }
}

function Test-ConcreteParkingBlocker {
  param([Parameter(Mandatory)] [string]$NextSafeAction)
  $match = [regex]::Match(
    $NextSafeAction.Trim(),
    '(?i)^BLOCKED\((?:permission|safety|scope|external)\):\s*(?<detail>\S(?:.*\S)?)$'
  )
  if (-not $match.Success) { return $false }
  $detail = $match.Groups['detail'].Value.Trim()
  if ($detail.Length -lt 12) { return $false }
  if ($detail -match '(?i)^(?:TBD|TODO|none|n/?a|unknown|blocked)[.!]?$') { return $false }
  if ($detail -match '(?i)^(?:awaiting|waiting(?:\s+for)?|needs?|pending)\b.*\b(?:review|reviewer|approval)\b') {
    return $false
  }
  return $true
}

function Assert-PrMapping {
  param(
    [Parameter(Mandatory)]$PullRequest,
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [string]$Branch,
    [Parameter(Mandatory)] [string]$TargetBranch
  )
  $prNumber = [int](Get-ObjectPropertyValue -InputObject $PullRequest -Name 'number' -Default 0)
  $head = [string](Get-ObjectPropertyValue -InputObject $PullRequest -Name 'headRefName' -Default '')
  $base = [string](Get-ObjectPropertyValue -InputObject $PullRequest -Name 'baseRefName' -Default '')
  if (-not [string]::Equals($head, $Branch, [System.StringComparison]::Ordinal)) {
    throw "PR #$prNumber head '$head' does not exactly match Issue #$IssueNumber handoff branch '$Branch'."
  }
  if (-not [string]::Equals($base, $TargetBranch, [System.StringComparison]::Ordinal)) {
    throw "PR #$prNumber base '$base' does not exactly match Issue #$IssueNumber target_branch '$TargetBranch'."
  }
  $closing = @(Get-ClosingIssueNumbers -PullRequest $PullRequest)
  if ($closing.Count -ne 1 -or $closing[0] -ne $IssueNumber) {
    throw "PR #$prNumber must close exactly Issue #$IssueNumber; found [$($closing -join ', ')]."
  }
}

function Assert-DeliveryInventory {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$OpenIssues,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$OpenPullRequests,
    [ValidateRange(-1, 2147483647)] [int]$SelectedIssue = -1,
    [switch]$DeferGreenDraftToDrain
  )

  $issuesToInspect = @($OpenIssues | Where-Object {
    $state = [string](Get-ObjectPropertyValue -InputObject $_ -Name 'state' -Default '')
    $number = [int](Get-ObjectPropertyValue -InputObject $_ -Name 'number' -Default 0)
    $state -ieq 'OPEN' -and ($SelectedIssue -lt 0 -or $number -eq $SelectedIssue)
  })
  if ($SelectedIssue -ge 0 -and $issuesToInspect.Count -ne 1) {
    throw "Scoped delivery audit expected exactly one open Issue #$SelectedIssue, found $($issuesToInspect.Count)."
  }

  $records = [System.Collections.Generic.List[object]]::new()
  foreach ($issueData in $issuesToInspect) {
    $number = [int](Get-ObjectPropertyValue -InputObject $issueData -Name 'number' -Default 0)
    $body = [string](Get-ObjectPropertyValue -InputObject $issueData -Name 'body' -Default '')
    $path = "issue/$number"
    $hasHandoff = $body.Contains($script:HandoffMarker)
    $labelNames = @(Get-IssueLabelNames -IssueData $issueData)
    # The Issue form applies execution-request outside its editable body. Keep
    # the legacy body marker as a compatibility signal for existing Issues.
    $hasExecutionRequest = $labelNames -contains 'execution-request' -or
      $body.Contains($script:ExecutionRequestMarker)
    if (-not $hasHandoff) {
      if ($hasExecutionRequest) {
        throw "Open execution Issue #$number is missing a concrete hibiki:handoff-v1 block."
      }
      continue
    }

    $block = Get-HandoffBlockText -Body $body -Path $path
    $unresolved = @(Get-UnresolvedHandoffKeys -Block $block)
    if ($unresolved.Count -gt 0) {
      throw "Open handoff Issue #$number contains unresolved placeholder field(s): $($unresolved -join ', ')."
    }
    $declaredIssueText = Get-HandoffScalar -Block $block -Key 'issue' -Path $path
    $declaredIssue = 0
    if (-not [int]::TryParse($declaredIssueText, [ref]$declaredIssue) -or $declaredIssue -ne $number) {
      throw "Handoff issue field must exactly match open Issue #$number; got '$declaredIssueText'."
    }
    $branch = Get-HandoffScalar -Block $block -Key 'branch' -Path $path
    $targetBranch = Get-HandoffScalar -Block $block -Key 'target_branch' -Path $path
    if ([string]::Equals($branch, $targetBranch, [System.StringComparison]::Ordinal)) {
      throw "Issue #$number handoff branch and target_branch must differ."
    }
    $nextSafeAction = Get-HandoffScalar -Block $block -Key 'next_safe_action' -Path $path
    $lifecycle = Get-LifecycleLabel -IssueData $issueData -Path $path
    [void]$records.Add([pscustomobject]@{
      Number = $number
      Branch = $branch
      TargetBranch = $targetBranch
      NextSafeAction = $nextSafeAction
      Lifecycle = $lifecycle
      PullRequest = $null
    })
  }

  $branchesInScope = @($records | ForEach-Object { $_.Branch })
  $branchGroups = @($OpenPullRequests |
    Where-Object {
      $head = [string](Get-ObjectPropertyValue -InputObject $_ -Name 'headRefName' -Default '')
      $head -and ($SelectedIssue -lt 0 -or $branchesInScope -ccontains $head)
    } |
    Group-Object -CaseSensitive -Property { [string](Get-ObjectPropertyValue -InputObject $_ -Name 'headRefName' -Default '') })
  foreach ($group in $branchGroups) {
    if ($group.Count -gt 1) {
      $numbers = @($group.Group | ForEach-Object { Get-ObjectPropertyValue -InputObject $_ -Name 'number' -Default 0 })
      throw "Branch '$($group.Name)' has more than one open PR: $($numbers -join ', ')."
    }
  }

  foreach ($record in $records) {
    $associatedByNumber = @{}
    foreach ($pullRequest in $OpenPullRequests) {
      $prNumber = [int](Get-ObjectPropertyValue -InputObject $pullRequest -Name 'number' -Default 0)
      $head = [string](Get-ObjectPropertyValue -InputObject $pullRequest -Name 'headRefName' -Default '')
      $closing = @(Get-ClosingIssueNumbers -PullRequest $pullRequest)
      if ([string]::Equals($head, $record.Branch, [System.StringComparison]::Ordinal) -or
          $closing -contains $record.Number) {
        $associatedByNumber[$prNumber] = $pullRequest
      }
    }
    $associated = @($associatedByNumber.Values)
    if ($associated.Count -gt 1) {
      $numbers = @($associated | ForEach-Object { Get-ObjectPropertyValue -InputObject $_ -Name 'number' -Default 0 })
      throw "Issue #$($record.Number) has more than one associated open PR: $($numbers -join ', ')."
    }

    if ($record.Lifecycle -eq 'claim-pending') {
      if ($associated.Count -ne 0) {
        throw "Issue #$($record.Number) is claim-pending and must not have an open PR yet."
      }
      continue
    }
    if ($associated.Count -eq 0) {
      if ($record.Lifecycle -eq 'in-review') {
        throw "Issue #$($record.Number) is in-review and requires exactly one non-draft open PR."
      }
      continue
    }

    $pr = $associated[0]
    $record.PullRequest = $pr
    Assert-PrMapping -PullRequest $pr -IssueNumber $record.Number -Branch $record.Branch `
      -TargetBranch $record.TargetBranch
    $prNumber = [int](Get-ObjectPropertyValue -InputObject $pr -Name 'number' -Default 0)
    $isDraft = [bool](Get-ObjectPropertyValue -InputObject $pr -Name 'isDraft' -Default $false)
    $checkState = Get-CurrentHeadCheckState -PullRequest $pr
    $headOid = [string](Get-ObjectPropertyValue -InputObject $pr -Name 'headRefOid' -Default 'unknown')
    if ($headOid -notmatch '^[0-9a-fA-F]{40}$') {
      throw "PR #$prNumber current head SHA is unavailable or malformed; exact-head audit cannot continue."
    }

    if ($record.Lifecycle -eq 'claimed') {
      if (-not $isDraft) {
        throw "Issue #$($record.Number) is claimed but PR #$prNumber is ready; move lifecycle to in-review."
      }
      if ($checkState.IsGreen -and -not $DeferGreenDraftToDrain -and
          -not (Test-ConcreteParkingBlocker -NextSafeAction $record.NextSafeAction)) {
        throw ("Green draft parking is not allowed: PR #$prNumber at current head $headOid has completed " +
          "verify and CodeQL analyze checks. Mark it ready and continue to merge, or record " +
          "BLOCKED(permission|safety|scope|external): <concrete detail> in next_safe_action.")
      }
      continue
    }

    if ($record.Lifecycle -eq 'in-review') {
      if ($isDraft) {
        throw "Issue #$($record.Number) is in-review but PR #$prNumber is still draft."
      }
      if (-not $checkState.IsGreen) {
        throw ("Issue #$($record.Number) is in-review but PR #$prNumber current head $headOid does not have " +
          "completed allowed checks including verify and CodeQL analyze contexts.")
      }
      continue
    }

    throw "Unsupported lifecycle '$($record.Lifecycle)' for Issue #$($record.Number)."
  }

  return @($records)
}

function Get-DrainCandidates {
  param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$Records)
  $candidates = [System.Collections.Generic.List[object]]::new()
  foreach ($record in $Records) {
    if ($null -eq $record.PullRequest) { continue }
    if (Test-ConcreteParkingBlocker -NextSafeAction ([string]$record.NextSafeAction)) { continue }
    $checkState = Get-CurrentHeadCheckState -PullRequest $record.PullRequest
    if (-not $checkState.IsGreen) { continue }
    $pr = $record.PullRequest
    [void]$candidates.Add([pscustomobject]@{
      Issue = [int]$record.Number
      PullRequest = [int](Get-ObjectPropertyValue -InputObject $pr -Name 'number' -Default 0)
      Draft = [bool](Get-ObjectPropertyValue -InputObject $pr -Name 'isDraft' -Default $false)
      MergeState = [string](Get-ObjectPropertyValue -InputObject $pr -Name 'mergeStateStatus' -Default 'UNKNOWN')
      HeadOid = [string](Get-ObjectPropertyValue -InputObject $pr -Name 'headRefOid' -Default 'unknown')
    })
  }
  return @($candidates)
}

function Assert-NoDrainCandidates {
  param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]]$Candidates)
  if ($Candidates.Count -eq 0) { return }
  $details = @($Candidates | Select-Object -First 8 | ForEach-Object {
    "PR #$($_.PullRequest) / Issue #$($_.Issue) (draft=$($_.Draft), mergeState=$($_.MergeState), head=$($_.HeadOid))"
  })
  if ($Candidates.Count -gt $details.Count) {
    $details += "+ $($Candidates.Count - $details.Count) more"
  }
  throw ("Before-explore drain gate found current-head green PR(s) without a concrete blocker: " +
    ($details -join '; ') + '. Drain by readying, syncing/reverifying when behind, and merging before discovering more work.')
}

function Assert-Throws {
  param(
    [Parameter(Mandatory)] [scriptblock]$Action,
    [Parameter(Mandatory)] [string]$Label,
    [string]$MessagePattern = ''
  )
  $caught = $null
  try { & $Action } catch { $caught = $_ }
  if ($null -eq $caught) { throw "delivery-audit self-test failed: expected rejection ($Label)." }
  if ($MessagePattern -and $caught.Exception.Message -notmatch $MessagePattern) {
    throw "delivery-audit self-test failed: '$Label' rejected with unexpected message: $($caught.Exception.Message)"
  }
}

if ($SelfTest) {
  $caseCount = 0
  function New-TestIssue {
    param(
      [int]$Number = 41,
      [string]$Branch = 'codex/41-delivery-selftest',
      [string]$TargetBranch = 'main',
      [string]$Lifecycle = 'claimed',
      [string]$NextSafeAction = 'Run the next focused implementation step.',
      [string]$IssueField = '',
      [switch]$Tbd,
      [switch]$ExecutionMarkerOnly
    )
    if ($ExecutionMarkerOnly) {
      return [pscustomobject]@{
        number = $Number; state = 'OPEN'; labels = @(); assignees = @()
        body = '<!-- hibiki:execution-request-v1 -->'
      }
    }
    if (-not $IssueField) { $IssueField = [string]$Number }
    if ($Tbd) { $Branch = 'codex/TBD-selftest' }
    $body = @(
      '<!-- hibiki:execution-request-v1 -->',
      '<!-- hibiki:handoff-v1',
      'schema_version: 2',
      "issue: $IssueField",
      "branch: $Branch",
      "target_branch: $TargetBranch",
      'owner: selftest-owner',
      'scope_globs: ["tools/selftest.ps1"]',
      "next_safe_action: `"$NextSafeAction`"",
      '-->'
    ) -join "`n"
    return [pscustomobject]@{
      number = $Number; state = 'OPEN'
      labels = @([pscustomobject]@{ name = $Lifecycle })
      assignees = @([pscustomobject]@{ login = 'selftest-owner' })
      body = $body
    }
  }

  function New-GreenChecks {
    return @(
      [pscustomobject]@{ name = 'verify'; workflowName = 'verify'; status = 'COMPLETED'; conclusion = 'SUCCESS' },
      [pscustomobject]@{ name = 'Analyze (c-cpp)'; workflowName = 'CodeQL'; status = 'COMPLETED'; conclusion = 'NEUTRAL' },
      [pscustomobject]@{ name = 'optional'; workflowName = 'optional'; status = 'COMPLETED'; conclusion = 'SKIPPED' }
    )
  }

  function New-PendingChecks {
    return @(
      [pscustomobject]@{ name = 'verify'; workflowName = 'verify'; status = 'IN_PROGRESS'; conclusion = '' },
      [pscustomobject]@{ name = 'Analyze (c-cpp)'; workflowName = 'CodeQL'; status = 'COMPLETED'; conclusion = 'SUCCESS' }
    )
  }

  function New-TestPr {
    param(
      [int]$Number = 51,
      [string]$Branch = 'codex/41-delivery-selftest',
      [string]$Base = 'main',
      [int[]]$Closing = @(41),
      [bool]$Draft = $true,
      [object[]]$Checks = @()
    )
    return [pscustomobject]@{
      number = $Number; state = 'OPEN'; isDraft = $Draft
      headRefName = $Branch; headRefOid = ('a' * 40); baseRefName = $Base
      mergeStateStatus = 'CLEAN'
      closingIssuesReferences = @($Closing | ForEach-Object { [pscustomobject]@{ number = $_ } })
      statusCheckRollup = @($Checks)
    }
  }

  # Passing states: claimed may have no PR or one active draft; in-review owns
  # one ready PR whose current-head verify and CodeQL analyze contexts are green.
  $claimed = New-TestIssue
  [void](Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @())
  $caseCount++
  $activeDraft = New-TestPr -Checks (New-PendingChecks)
  [void](Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($activeDraft))
  $caseCount++
  $inReview = New-TestIssue -Lifecycle 'in-review'
  $readyGreen = New-TestPr -Draft $false -Checks (New-GreenChecks)
  $readyGreen.mergeStateStatus = 'BEHIND'
  $inReviewRecords = @(Assert-DeliveryInventory -OpenIssues @($inReview) -OpenPullRequests @($readyGreen))
  $caseCount++
  $drainCandidates = @(Get-DrainCandidates -Records $inReviewRecords)
  if ($drainCandidates.Count -ne 1 -or $drainCandidates[0].PullRequest -ne 51 -or
      $drainCandidates[0].MergeState -ne 'BEHIND') {
    throw 'delivery-audit self-test failed: current-head green behind PR was not selected for drain.'
  }
  Assert-Throws {
    Assert-NoDrainCandidates -Candidates $drainCandidates
  } 'before-explore drain candidate' 'Before-explore drain gate'
  $caseCount++

  Assert-Throws {
    Assert-DeliveryInventory -OpenIssues @((New-TestIssue -Tbd)) -OpenPullRequests @()
  } 'unresolved TBD handoff' 'unresolved placeholder'
  $caseCount++
  Assert-Throws {
    Assert-DeliveryInventory -OpenIssues @((New-TestIssue -ExecutionMarkerOnly)) -OpenPullRequests @()
  } 'execution request missing handoff' 'missing a concrete'
  $caseCount++
  $labelOnlyExecution = [pscustomobject]@{
    number = 42; state = 'OPEN'; body = ''
    labels = @([pscustomobject]@{ name = 'execution-request' }); assignees = @()
  }
  Assert-Throws {
    Assert-DeliveryInventory -OpenIssues @($labelOnlyExecution) -OpenPullRequests @()
  } 'label-classified execution request missing handoff' 'missing a concrete'
  $caseCount++

  Assert-Throws {
    $wrongLink = New-TestPr -Closing @(42) -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($wrongLink)
  } 'wrong closing Issue' 'must close exactly'
  $caseCount++
  Assert-Throws {
    $multipleLinks = New-TestPr -Closing @(41, 42) -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($multipleLinks)
  } 'multiple closing Issues' 'must close exactly'
  $caseCount++
  Assert-Throws {
    $wrongHead = New-TestPr -Branch 'codex/other-head' -Closing @(41) -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($wrongHead)
  } 'wrong PR head' 'does not exactly match'
  $caseCount++
  Assert-Throws {
    $wrongBase = New-TestPr -Base 'release' -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($wrongBase)
  } 'wrong PR base' 'does not exactly match'
  $caseCount++

  Assert-Throws {
    $first = New-TestPr -Number 51 -Checks (New-PendingChecks)
    $second = New-TestPr -Number 52 -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($first, $second)
  } 'duplicate branch PR' 'more than one open PR'
  $caseCount++

  Assert-Throws {
    $ready = New-TestPr -Draft $false -Checks (New-GreenChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($ready)
  } 'claimed with ready PR' 'move lifecycle to in-review'
  $caseCount++
  Assert-Throws {
    $draft = New-TestPr -Draft $true -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($inReview) -OpenPullRequests @($draft)
  } 'in-review with draft PR' 'still draft'
  $caseCount++
  Assert-Throws {
    $readyPending = New-TestPr -Draft $false -Checks (New-PendingChecks)
    Assert-DeliveryInventory -OpenIssues @($inReview) -OpenPullRequests @($readyPending)
  } 'in-review current head not green' 'current head'
  $caseCount++
  Assert-Throws {
    $unknownHead = New-TestPr -Checks (New-PendingChecks)
    $unknownHead.headRefOid = 'stale-or-unavailable'
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($unknownHead)
  } 'unavailable current head identity' 'exact-head audit cannot continue'
  $caseCount++

  Assert-Throws {
    $greenDraft = New-TestPr -Checks (New-GreenChecks)
    Assert-DeliveryInventory -OpenIssues @($claimed) -OpenPullRequests @($greenDraft)
  } 'green draft parking' 'Green draft parking'
  $caseCount++
  $blocked = New-TestIssue -NextSafeAction 'BLOCKED(permission): Maintainer must grant ruleset administration access.'
  [void](Assert-DeliveryInventory -OpenIssues @($blocked) -OpenPullRequests @((New-TestPr -Checks (New-GreenChecks))))
  $caseCount++
  Assert-Throws {
    $fakeBlocked = New-TestIssue -NextSafeAction 'BLOCKED(external): Waiting for review.'
    Assert-DeliveryInventory -OpenIssues @($fakeBlocked) -OpenPullRequests @((New-TestPr -Checks (New-GreenChecks)))
  } 'vague review blocker' 'Green draft parking'
  $caseCount++

  # A scoped audit must ignore an unrelated malformed Issue while still using
  # the complete PR inventory for the selected branch.
  $unrelatedBad = New-TestIssue -Number 99 -Branch 'codex/TBD-other' -Tbd
  [void](Assert-DeliveryInventory -OpenIssues @($claimed, $unrelatedBad) -OpenPullRequests @() -SelectedIssue 41)
  $caseCount++

  Write-Output ("delivery-audit self-test passed ($caseCount cases: pass states, TBD/missing handoff, " +
    'PR mapping/base/link uniqueness, lifecycle consistency, exact-head checks, green-draft parking/blockers, before-explore drain, scoped isolation).')
  exit 0
}

function ConvertFrom-GhJson {
  param(
    [Parameter(Mandatory)] [string[]]$Arguments,
    [Parameter(Mandatory)] [string]$Description
  )
  $json = & gh @Arguments 2>&1
  if ($LASTEXITCODE -ne 0) { throw "$Description failed via gh: $json" }
  try { return @($json | ConvertFrom-Json) }
  catch { throw "$Description returned invalid JSON: $($_.Exception.Message)" }
}

$inventoryCap = 500
if ($BeforeExplore -and $Issue -ge 0) {
  throw '-BeforeExplore is a repository-wide drain gate and cannot be combined with scoped -Issue.'
}
if ($Issue -ge 0) {
  $issues = @(ConvertFrom-GhJson -Arguments @(
    'issue', 'view', "$Issue", '--json', 'number,state,title,body,labels,assignees,url'
  ) -Description "Issue #$Issue inventory")
} else {
  $issues = @(ConvertFrom-GhJson -Arguments @(
    'issue', 'list', '--state', 'open', '--limit', "$inventoryCap",
    '--json', 'number,state,title,body,labels,assignees,url'
  ) -Description 'Open Issue inventory')
  if ($issues.Count -ge $inventoryCap) {
    throw "Open Issue inventory reached cap $inventoryCap; raise the explicit cap rather than accepting truncation."
  }
}

$pullRequests = @(ConvertFrom-GhJson -Arguments @(
  'pr', 'list', '--state', 'open', '--limit', "$inventoryCap",
  '--json', 'number,state,title,isDraft,headRefName,headRefOid,baseRefName,closingIssuesReferences,statusCheckRollup,mergeStateStatus,url'
) -Description 'Open PR inventory')
if ($pullRequests.Count -ge $inventoryCap) {
  throw "Open PR inventory reached cap $inventoryCap; raise the explicit cap rather than accepting truncation."
}

$audited = @(Assert-DeliveryInventory -OpenIssues $issues -OpenPullRequests $pullRequests -SelectedIssue $Issue `
  -DeferGreenDraftToDrain:$BeforeExplore)
if ($BeforeExplore) {
  $drainCandidates = @(Get-DrainCandidates -Records $audited)
  Assert-NoDrainCandidates -Candidates $drainCandidates
}
$scopeText = if ($Issue -ge 0) { "Issue #$Issue" } else { 'all open execution handoffs' }
Write-Output ("Delivery audit passed for $scopeText ($($audited.Count) handoff(s), " +
  "$($pullRequests.Count) bounded open PR(s) inspected).")
