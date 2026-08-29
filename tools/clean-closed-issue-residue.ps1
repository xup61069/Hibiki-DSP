#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$DryRun,
  [int]$Limit = 0,
  [ValidateSet('closed')]
  [string]$State = 'closed',
  [switch]$SelfTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$lifecycleLabels = @('claimed', 'claim-pending', 'in-review')

function Get-ResidueItems {
  param([Parameter(Mandatory)] [object]$Issue)
  $names = @($Issue.labels | ForEach-Object { [string]$_.name })
  $labels = @($names | Where-Object { $_ -in $script:lifecycleLabels })
  $assignees = @($Issue.assignees | ForEach-Object { [string]$_.login })
  return [pscustomobject]@{ Labels = $labels; Assignees = $assignees }
}

function Get-IssueItemsFromPages {
  param(
    [Parameter(Mandatory)] [object[]]$Pages,
    [Parameter(Mandatory)] [int]$Limit
  )
  if ($Limit -lt 0) { throw 'Limit must be zero (unbounded) or a positive issue count.' }

  $issues = [System.Collections.Generic.List[object]]::new()
  foreach ($page in @($Pages)) {
    foreach ($item in @($page)) {
      if ($null -eq $item) { continue }
      # The REST /issues endpoint includes pull requests. They are not Issues
      # and must never be edited by this residue sweep.
      if ($item.PSObject.Properties.Name -contains 'pull_request') { continue }
      [void]$issues.Add([pscustomobject]@{
          number = [int]$item.number
          title = [string]$item.title
          labels = @($item.labels)
          assignees = @($item.assignees)
        })
      if ($Limit -gt 0 -and $issues.Count -gt $Limit) {
        throw "Configured issue limit $Limit exhausted while paginating closed issues; increase Limit or use Limit 0 for the complete set."
      }
    }
  }
  return $issues.ToArray()
}

function Get-IssueBatch {
  param(
    [Parameter(Mandatory)] [string]$State,
    [Parameter(Mandatory)] [int]$Limit
  )
  $endpoint = 'repos/{owner}/{repo}/issues?state=' +
    [uri]::EscapeDataString($State) + '&per_page=100'
  $raw = & gh api --paginate --slurp $endpoint 2>&1
  if ($LASTEXITCODE -ne 0) { throw "GitHub Issues pagination failed: $raw" }
  try {
    $pages = @(($raw | ConvertFrom-Json))
  } catch {
    throw "GitHub Issues pagination returned invalid JSON: $($_.Exception.Message)"
  }
  return @(Get-IssueItemsFromPages -Pages $pages -Limit $Limit)
}

function Clear-IssueResidue {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Labels,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Assignees,
    [switch]$WhatIfMode
  )
  foreach ($label in $Labels) {
    if ($WhatIfMode) { Write-Output ('[dry-run] remove-label: ' + $label) }
    else {
      gh issue edit $IssueNumber --remove-label $label | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "Unable to remove label '$label' from Issue #$IssueNumber." }
    }
  }
  foreach ($owner in $Assignees) {
    if ($WhatIfMode) { Write-Output ('[dry-run] remove-assignee: ' + $owner) }
    else {
      gh issue edit $IssueNumber --remove-assignee $owner | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "Unable to remove assignee '$owner' from Issue #$IssueNumber." }
    }
  }
}

if ($SelfTest) {
  $withResidue = [pscustomobject]@{
    labels = @([pscustomobject]@{ name = 'claimed' }, [pscustomobject]@{ name = 'enhancement' })
    assignees = @([pscustomobject]@{ login = 'someone' })
  }
  $r1 = Get-ResidueItems -Issue $withResidue
  if (@($r1.Labels).Count -ne 1 -or @($r1.Labels)[0] -ne 'claimed') { throw 'self-test failed: label residue not detected.' }
  if (@($r1.Assignees).Count -ne 1) { throw 'self-test failed: assignee residue not detected.' }

  $cleanIssue = [pscustomobject]@{
    labels = @([pscustomobject]@{ name = 'enhancement' })
    assignees = @()
  }
  $r2 = Get-ResidueItems -Issue $cleanIssue
  if (@($r2.Labels).Count -ne 0 -or @($r2.Assignees).Count -ne 0) { throw 'self-test failed: clean issue flagged as residue.' }

  $allLifecycle = [pscustomobject]@{
    labels = @([pscustomobject]@{ name = 'claimed' }, [pscustomobject]@{ name = 'claim-pending' }, [pscustomobject]@{ name = 'in-review' })
    assignees = @()
  }
  $r3 = Get-ResidueItems -Issue $allLifecycle
  if (@($r3.Labels).Count -ne 3) { throw 'self-test failed: multiple lifecycle labels not all detected.' }

  $pageOne = @(
    [pscustomobject]@{ number = 101; title = 'first issue'; labels = @(); assignees = @() }
    [pscustomobject]@{ number = 102; title = 'pull request'; labels = @(); assignees = @(); pull_request = [pscustomobject]@{} }
  )
  $pageTwo = @(
    [pscustomobject]@{ number = 103; title = 'second issue'; labels = @(); assignees = @() }
  )
  $flattened = @(Get-IssueItemsFromPages -Pages @($pageOne, $pageTwo) -Limit 0)
  if (@($flattened).Count -ne 2 -or
      @($flattened | Where-Object number -eq 102).Count -ne 0) {
    throw 'self-test failed: pagination did not filter pull requests.'
  }

  $limitFailed = $false
  try { [void](Get-IssueItemsFromPages -Pages @($pageOne, $pageTwo) -Limit 1) } catch { $limitFailed = $true }
  if (-not $limitFailed) { throw 'self-test failed: exhausted configured limit did not fail closed.' }

  $cleanResidue = Get-ResidueItems -Issue $flattened[0]
  if (@($cleanResidue.Labels).Count -ne 0 -or @($cleanResidue.Assignees).Count -ne 0) {
    throw 'self-test failed: clean paginated Issue was not recognized as zero residue.'
  }

  Write-Output 'clean-closed-issue-residue self-test passed (pagination, PR filtering, bound exhaustion, residue classification, and clean result).'
  exit 0
}

$totalScanned = 0
$totalWithResidue = 0
$totalCleaned = 0
$batch = @(Get-IssueBatch -State $State -Limit $Limit)
foreach ($issue in @($batch)) {
  $totalScanned++
  $residue = Get-ResidueItems -Issue $issue
  if ((@($residue.Labels)).Count -gt 0 -or (@($residue.Assignees)).Count -gt 0) {
    $totalWithResidue++
    Write-Output ('#' + $issue.number + ': labels=[' + (($residue.Labels) -join ',') + '] assignees=[' + (($residue.Assignees) -join ',') + ']')
    if (-not $DryRun) {
      Clear-IssueResidue -IssueNumber $issue.number -Labels @($residue.Labels) -Assignees @($residue.Assignees)
      $totalCleaned++
    }
  }
}

Write-Output ('scanned=' + $totalScanned + ' with_residue=' + $totalWithResidue + ' cleaned=' + $totalCleaned + ' limit=' + $Limit + ' dry_run=' + $DryRun)
