#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$DryRun,
  [int]$Limit = 100,
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

function Clear-IssueResidue {
  param(
    [Parameter(Mandatory)] [int]$IssueNumber,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Labels,
    [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]]$Assignees,
    [switch]$WhatIfMode
  )
  foreach ($label in $Labels) {
    if ($WhatIfMode) { Write-Output ('[dry-run] remove-label: ' + $label) }
    else { gh issue edit $IssueNumber --remove-label $label | Out-Null }
  }
  foreach ($owner in $Assignees) {
    if ($WhatIfMode) { Write-Output ('[dry-run] remove-assignee: ' + $owner) }
    else { gh issue edit $IssueNumber --remove-assignee $owner | Out-Null }
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

  Write-Output 'clean-closed-issue-residue self-test passed (3 cases).'
  exit 0
}

$totalScanned = 0
$totalWithResidue = 0
$totalCleaned = 0
$batch = gh issue list --state $State --limit $Limit --json number,title,labels,assignees --jq '.' | ConvertFrom-Json
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

Write-Output ('scanned=' + $totalScanned + ' with_residue=' + $totalWithResidue + ' cleaned=' + $totalCleaned + ' dry_run=' + $DryRun)
