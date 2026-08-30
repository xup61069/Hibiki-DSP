#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest,
  [string]$SessionId,
  [int]$Issue,
  [string]$Branch
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-HandoffBranch {
  param([Parameter(Mandatory)] [string]$Body)
  # Extract only the handoff block so a stray 'branch:' elsewhere in the body
  # cannot cause a false match or rejection.
  if (-not $Body.Contains('hibiki:handoff-v1')) { throw 'Issue body is missing the hibiki:handoff-v1 block.' }
  $handoffMatch = [regex]::Match($Body, '(?s)<!-- hibiki:handoff-v1\s*(?<block>.*?)\s*-->')
  if (-not $handoffMatch.Success -or [string]::IsNullOrWhiteSpace($handoffMatch.Groups['block'].Value)) {
    throw 'Handoff block is empty or unterminated.'
  }
  # Normalize CRLF before line-anchored matching; otherwise CR sits between the
  # captured text and LF and breaks the end anchor.
  $normalizedBlock = $handoffMatch.Groups['block'].Value -replace "`r`n", "`n"
  $branchLine = [regex]::Match($normalizedBlock, '(?im)^branch\s*:\s*(?<value>[^\r\n]+)$')
  if (-not $branchLine.Success) { throw 'Handoff block missing branch field.' }
  return $branchLine.Groups['value'].Value.Trim().Trim('"').Trim("'")
}

function Invoke-RemoteBranchProbe {
  param([Parameter(Mandatory)] [string]$BranchName)
  # Keep this as the only ls-remote invocation. Its output and exit code belong
  # to the same probe, so a transient failure cannot be mistaken for an absent
  # branch by a second, different network read.
  $output = @(& git ls-remote --heads origin $BranchName 2>&1)
  $exitCode = $LASTEXITCODE
  return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

function Assert-RemoteBranchAvailable {
  param(
    [Parameter(Mandatory)] [string]$BranchName,
    [Parameter(Mandatory)]$Probe
  )
  $probeText = (@($Probe.Output) | ForEach-Object { [string]$_ }) -join "`n"
  if ([int]$Probe.ExitCode -ne 0) {
    throw "Remote branch query failed for '$BranchName' (exit $($Probe.ExitCode)); fail closed: $probeText"
  }
  if (-not [string]::IsNullOrWhiteSpace($probeText)) {
    throw "Remote branch '$BranchName' already exists."
  }
}

if ($SelfTest) {
  $passedCases = 0
  # LF body with a stray branch outside the block: must pick the block value.
  $lfBody = "context text`nbranch: wrong/branch`n`n<!-- hibiki:handoff-v1`nissue: 1`nbranch: codex/right`n-->"
  $resultLf = Get-HandoffBranch -Body $lfBody
  if ($resultLf -ne 'codex/right') { throw "Self-test failed: LF stray-branch expected codex/right, got '$resultLf'." }
  $passedCases++

  # CRLF body inside the block: must normalize before line-anchored matching.
  $crlfBody = "prelude`r`nbranch: wrong/other`r`n`r`n<!-- hibiki:handoff-v1`r`nissue: 2`r`nbranch: `"codex/crlf`"`r`n-->`r`ntrailing"
  $resultCrlf = Get-HandoffBranch -Body $crlfBody
  if ($resultCrlf -ne 'codex/crlf') { throw "Self-test failed: CRLF expected codex/crlf, got '$resultCrlf'." }
  $passedCases++

  # Missing block must throw.
  $caughtMissing = $false
  try { Get-HandoffBranch -Body 'no block here' } catch { $caughtMissing = $true }
  if (-not $caughtMissing) { throw 'Self-test failed: missing block did not throw.' }
  $passedCases++

  # Unterminated block must throw.
  $caughtUnterminated = $false
  try { Get-HandoffBranch -Body '<!-- hibiki:handoff-v1 branch: codex/no-close' } catch { $caughtUnterminated = $true }
  if (-not $caughtUnterminated) { throw 'Self-test failed: unterminated block did not throw.' }
  $passedCases++

  # A failed remote read is not evidence that a branch is absent.
  $caughtRemoteFailure = $false
  try {
    Assert-RemoteBranchAvailable -BranchName 'codex/failure' -Probe ([pscustomobject]@{
      ExitCode = 128; Output = @('fatal: unable to access remote')
    })
  } catch {
    $caughtRemoteFailure = $_.Exception.Message -match 'query failed' -and $_.Exception.Message -match 'exit 128'
  }
  if (-not $caughtRemoteFailure) { throw 'Self-test failed: remote-query failure did not fail closed.' }
  $passedCases++

  # One successful non-empty probe proves the branch already exists.
  $caughtExistingBranch = $false
  try {
    Assert-RemoteBranchAvailable -BranchName 'codex/existing' -Probe ([pscustomobject]@{
      ExitCode = 0; Output = @('0123456789abcdef refs/heads/codex/existing')
    })
  } catch {
    $caughtExistingBranch = $_.Exception.Message -match 'already exists'
  }
  if (-not $caughtExistingBranch) { throw 'Self-test failed: existing remote branch was accepted.' }
  $passedCases++

  Assert-RemoteBranchAvailable -BranchName 'codex/available' -Probe ([pscustomobject]@{
    ExitCode = 0; Output = @()
  })
  $passedCases++

  Write-Output "claim-issue self-test passed ($passedCases cases: LF/CRLF extraction, block isolation, remote failure/existing/available branch handling)."
  exit 0
}

if ([string]::IsNullOrWhiteSpace($SessionId)) { throw 'SessionId is required.' }
if ($Issue -le 0) { throw 'Issue is required.' }
if ([string]::IsNullOrWhiteSpace($Branch)) { throw 'Branch is required.' }

# Validate UUID format
if ($SessionId -notmatch '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$') {
  throw 'SessionId must be a valid UUID.'
}

# Read the issue fresh.
# Use $issueData because PowerShell variable names are case-insensitive;
# assigning to lowercase $issue would overwrite the mandatory [int]$Issue
# parameter with a PSCustomObject and fail closed before any validation runs.
$issueJson = gh issue view $Issue --json number,state,title,body,labels,assignees 2>&1
if ($LASTEXITCODE -ne 0) { throw "Could not read Issue #$Issue : $issueJson" }
$issueData = $issueJson | ConvertFrom-Json

if ($issueData.state -ne 'OPEN') { throw "Issue #$Issue is not open." }
$labelNames = @($issueData.labels | ForEach-Object { $_.name })
if ($labelNames -contains 'claimed' -or $labelNames -contains 'in-review') { throw "Issue #$Issue already has claimed/in-review; cannot admit." }
if (@($issueData.assignees).Count -gt 0) { throw "Issue #$Issue already has an assignee; cannot admit." }

$declaredBranch = Get-HandoffBranch -Body $issueData.body
if ($declaredBranch -ne $Branch) { throw "Declared branch '$declaredBranch' does not match requested '$Branch'." }
if ($Branch -eq 'main') { throw 'Cannot claim main as a working branch.' }

# Query once, then validate both the exit code and the returned ref from that
# exact observation. Never retry implicitly and interpret failure as absence.
$remoteProbe = Invoke-RemoteBranchProbe -BranchName $Branch
Assert-RemoteBranchAvailable -BranchName $Branch -Probe $remoteProbe

Write-Output ('claim-issue: validated #' + $Issue + ' branch=' + $Branch + ' session=' + $SessionId)
Write-Output 'Next: dispatch claim-admission workflow to atomically add claim-pending label.'
