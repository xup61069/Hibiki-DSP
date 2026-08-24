#Requires -Version 7
[CmdletBinding()]
param(
  [Parameter(Mandatory)] [string]$SessionId,
  [Parameter(Mandatory)] [int]$Issue,
  [Parameter(Mandatory)] [string]$Branch
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Validate UUID format
if ($SessionId -notmatch '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$') {
  throw 'SessionId must be a valid UUID.'
}

# Read the issue fresh
$issueJson = gh issue view $Issue --json number,state,title,body,labels,assignees 2>&1
if ($LASTEXITCODE -ne 0) { throw "Could not read Issue #$Issue : $issueJson" }
$issue = $issueJson | ConvertFrom-Json

if ($issue.state -ne 'OPEN') { throw "Issue #$Issue is not open." }
$labelNames = @($issue.labels | ForEach-Object { $_.name })
if ($labelNames -contains 'claimed' -or $labelNames -contains 'in-review') { throw "Issue #$Issue already has claimed/in-review; cannot admit." }
if (@($issue.assignees).Count -gt 0) { throw "Issue #$Issue already has an assignee; cannot admit." }
if ($issue.body -notmatch 'hibiki:handoff-v1') { throw "Issue #$Issue missing handoff block." }

# Verify declared branch matches
$branchLine = [regex]::Match($issue.body, '(?im)^branch\s*:\s*(.+)$')
if (-not $branchLine.Success) { throw 'Handoff block missing branch field.' }
$declaredBranch = $branchLine.Groups[1].Value.Trim().Trim('"').Trim("'")
if ($declaredBranch -ne $Branch) { throw "Declared branch '$declaredBranch' does not match requested '$Branch'." }
if ($Branch -eq 'main') { throw 'Cannot claim main as a working branch.' }

# Check remote branch does not exist
git ls-remote --heads origin $Branch | Out-Null

$remoteRef = git ls-remote --heads origin $Branch 2>$null
if ($remoteRef) { throw "Remote branch '$Branch' already exists." }

Write-Output ('claim-issue: validated #' + $Issue + ' branch=' + $Branch + ' session=' + $SessionId)
Write-Output 'Next: dispatch claim-admission workflow to atomically add claim-pending label.'
