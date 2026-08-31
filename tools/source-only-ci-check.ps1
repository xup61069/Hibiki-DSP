#Requires -Version 7
[CmdletBinding()]
param(
    [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$script:BlockedCiPatterns = @(
    'upload-artifact',
    'actions/upload-artifact',
    'gh\s+release\s+upload',
    'softprops/action-gh-release',
    'docker\s+push',
    'npm\s+publish',
    'nuget\s+push',
    'packages:\s*write',
    'id-token:\s*write',
    'GUMROAD',
    'PARTNER_CENTER'
)

$script:BlockedBinaryExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj',
    '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')

function Get-WorkflowBlockEnd {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][int]$StartIndex,
        [Parameter(Mandatory)][int]$Indent
    )

    for ($index = $StartIndex + 1; $index -lt $Lines.Count; $index++) {
        $line = $Lines[$index]
        if ([string]::IsNullOrWhiteSpace($line) -or $line -match '^\s*#') { continue }
        $leadingSpaces = ([regex]::Match($line, '^ *')).Value.Length
        if ($leadingSpaces -le $Indent) { return $index }
    }
    return $Lines.Count
}

function Get-WorkflowMapKeyIndices {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][int]$StartIndex,
        [Parameter(Mandatory)][int]$EndExclusive,
        [Parameter(Mandatory)][int]$Indent,
        [Parameter(Mandatory)][string]$Key
    )

    $prefix = ' ' * $Indent
    $pattern = '^' + [regex]::Escape($prefix + $Key + ':') + '(?:\s+#.*)?\s*$'
    return @($StartIndex..($EndExclusive - 1) | Where-Object { $Lines[$_] -match $pattern })
}

function Get-WorkflowScalarKeyLines {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][int]$StartIndex,
        [Parameter(Mandatory)][int]$EndExclusive,
        [Parameter(Mandatory)][int]$Indent,
        [Parameter(Mandatory)][string]$Key
    )

    $prefix = ' ' * $Indent
    $pattern = '^' + [regex]::Escape($prefix + $Key + ':') + '(?<value>.*)$'
    $scalarResults = @()
    for ($index = $StartIndex; $index -lt $EndExclusive; $index++) {
        if ($Lines[$index] -match $pattern) {
            $scalarResults += [pscustomobject]@{ Index = $index; Value = $Matches['value'] }
        }
    }
    return @($scalarResults)
}

function Test-WorkflowStepScalar {
    param(
        [Parameter(Mandatory)][string[]]$Lines,
        [Parameter(Mandatory)][int]$StartIndex,
        [Parameter(Mandatory)][int]$EndExclusive,
        [Parameter(Mandatory)][string]$Key,
        [Parameter(Mandatory)][string]$ValuePattern
    )

    $values = @(Get-WorkflowScalarKeyLines -Lines $Lines -StartIndex $StartIndex -EndExclusive $EndExclusive -Indent 8 -Key $Key)
    return ($values.Count -eq 1 -and $values[0].Value -match $ValuePattern)
}

function Get-ForbiddenCiPattern {
    param([string]$Text)
    foreach ($pattern in $script:BlockedCiPatterns) {
        if ($Text -match "(?i)$pattern") { return $pattern }
    }
    return $null
}

function Test-WorkflowText {
    param([string]$Name, [string]$Text)
    $forbidden = Get-ForbiddenCiPattern -Text $Text
    if ($forbidden) {
        throw "Source-only CI violation '$forbidden' in $Name."
    }
    return ($Text -match '(?i)tools/source-policy\.ps1')
}

function Assert-WorkflowPolicyReferencePresent {
    param([bool]$Any)
    if (-not $Any) { throw 'A workflow must run tools/source-policy.ps1.' }
}

function Assert-VerifyTagProvenanceWiring {
    param([Parameter(Mandatory)][string]$Text)

    # This deliberately accepts only the repository's small YAML shape. A raw
    # text match could mistake a comment or literal scalar for executable YAML.
    $lines = @($Text -split '\r?\n' | Where-Object { $_.Length -gt 0 })
    $topLevelEnd = $lines.Count
    $onIndices = @(Get-WorkflowMapKeyIndices -Lines $lines -StartIndex 0 -EndExclusive $topLevelEnd -Indent 0 -Key 'on')
    if ($onIndices.Count -ne 1) {
        throw "verify.yml must contain one top-level on mapping."
    }
    $onEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $onIndices[0] -Indent 0
    $pushIndices = @(Get-WorkflowMapKeyIndices -Lines $lines -StartIndex ($onIndices[0] + 1) -EndExclusive $onEnd -Indent 2 -Key 'push')
    if ($pushIndices.Count -ne 1) {
        throw "verify.yml must contain one on.push mapping."
    }
    $pushEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $pushIndices[0] -Indent 2
    $tagLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($pushIndices[0] + 1) -EndExclusive $pushEnd -Indent 4 -Key 'tags')
    if ($tagLines.Count -ne 1 -or $tagLines[0].Value -notmatch '^\s*\[\s*[''"]v\*[''"]\s*\](?:\s+#.*)?\s*$') {
        throw "verify.yml must trigger on v* source tags."
    }

    $jobsIndices = @(Get-WorkflowMapKeyIndices -Lines $lines -StartIndex 0 -EndExclusive $topLevelEnd -Indent 0 -Key 'jobs')
    if ($jobsIndices.Count -ne 1) {
        throw "verify.yml must contain one top-level jobs mapping."
    }
    $jobsEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $jobsIndices[0] -Indent 0
    $verifyIndices = @(Get-WorkflowMapKeyIndices -Lines $lines -StartIndex ($jobsIndices[0] + 1) -EndExclusive $jobsEnd -Indent 2 -Key 'verify')
    if ($verifyIndices.Count -ne 1) {
        throw "verify.yml must contain one jobs.verify mapping."
    }
    $verifyEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $verifyIndices[0] -Indent 2
    $jobIfLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($verifyIndices[0] + 1) -EndExclusive $verifyEnd -Indent 4 -Key 'if')
    if ($jobIfLines.Count -ne 0) {
        throw "verify.yml jobs.verify cannot use a job-level if; source-tag gating must remain step-scoped."
    }
    $jobContinueOnErrorLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($verifyIndices[0] + 1) -EndExclusive $verifyEnd -Indent 4 -Key 'continue-on-error')
    if ($jobContinueOnErrorLines.Count -ne 0) {
        throw "verify.yml jobs.verify cannot use job-level continue-on-error."
    }
    $jobNeedsLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($verifyIndices[0] + 1) -EndExclusive $verifyEnd -Indent 4 -Key 'needs')
    if ($jobNeedsLines.Count -ne 0) {
        throw "verify.yml jobs.verify cannot use needs; source-tag gating must not depend on a skipped job."
    }
    $jobStrategyLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($verifyIndices[0] + 1) -EndExclusive $verifyEnd -Indent 4 -Key 'strategy')
    if ($jobStrategyLines.Count -ne 0) {
        throw "verify.yml jobs.verify cannot use strategy; source-tag gating must not depend on a matrix."
    }
    $stepsIndices = @(Get-WorkflowMapKeyIndices -Lines $lines -StartIndex ($verifyIndices[0] + 1) -EndExclusive $verifyEnd -Indent 4 -Key 'steps')
    if ($stepsIndices.Count -ne 1) {
        throw "verify.yml must contain one jobs.verify.steps sequence."
    }
    $stepsEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $stepsIndices[0] -Indent 4

    $validStep = $false
    for ($index = $stepsIndices[0] + 1; $index -lt $stepsEnd; $index++) {
        if ($lines[$index] -notmatch '^ {6}- name:\s*Source-tag provenance gate(?:\s+#.*)?\s*$') { continue }
        $stepEnd = Get-WorkflowBlockEnd -Lines $lines -StartIndex $index -Indent 6
        $stepContinueOnErrorLines = @(Get-WorkflowScalarKeyLines -Lines $lines -StartIndex ($index + 1) -EndExclusive $stepEnd -Indent 8 -Key 'continue-on-error')
        if ($stepContinueOnErrorLines.Count -ne 0) {
            throw "Source-tag provenance gate cannot set continue-on-error."
        }
        $hasTagGuard = Test-WorkflowStepScalar -Lines $lines -StartIndex ($index + 1) -EndExclusive $stepEnd -Key 'if' -ValuePattern '^\s*github\.ref_type\s*==\s*[''"]tag[''"](?:\s+#.*)?\s*$'
        $hasPwsh = Test-WorkflowStepScalar -Lines $lines -StartIndex ($index + 1) -EndExclusive $stepEnd -Key 'shell' -ValuePattern '^\s*pwsh(?:\s+#.*)?\s*$'
        $hasGateCommand = Test-WorkflowStepScalar -Lines $lines -StartIndex ($index + 1) -EndExclusive $stepEnd -Key 'run' -ValuePattern '^\s*''&\s+"\$env:GITHUB_WORKSPACE/tools/release-provenance-check\.ps1"\s+-Tag\s+\$env:GITHUB_REF_NAME''(?:\s+#.*)?\s*$'
        if ($hasTagGuard -and $hasPwsh -and $hasGateCommand) {
            $validStep = $true
            break
        }
    }
    if (-not $validStep) {
        throw "verify.yml must include the tag-scoped Source-tag provenance gate."
    }
}

function Get-BlockedTrackedPaths {
    param([string[]]$Paths)
    return @($Paths | Where-Object {
        $script:BlockedBinaryExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant()
    })
}

if ($SelfTest) {
    function Assert-GateRejection {
        param([scriptblock]$Action, [string]$ExpectedPattern, [string]$Label)
        try { & $Action } catch {
            if ("$($_.Exception.Message)" -notmatch $ExpectedPattern) {
                throw ("source-only-ci-check self-test case '{0}' failed with an unexpected message: {1}") -f $Label, $_.Exception.Message
            }
            return
        }
        throw ("source-only-ci-check self-test case '{0}' expected a rejection matching '{1}' but the gate passed.") -f $Label, $ExpectedPattern
    }

    $caseCount = 0
    $cleanWorkflow = "name: verify`njobs:`n  verify:`n    steps:`n      - name: Source gate`n        run: ./tools/source-policy.ps1"

    # Case 1: clean workflow passes and its policy reference is detected.
    $referencesPolicy = Test-WorkflowText -Name 'verify.yml' -Text $cleanWorkflow
    if (-not $referencesPolicy) { throw "source-only-ci-check self-test case 'clean-workflow' did not detect the source-policy reference." }
    $caseCount++

    # Cases 2..11: every forbidden publication pattern class is detected and named.
    $patternSamples = @(
        @{ Expected = 'upload-artifact';      Sample = 'uses: actions/upload-artifact@v4' },
        @{ Expected = 'release\s+upload';     Sample = 'run: gh release upload dist/tool.zip' },
        @{ Expected = 'action-gh-release';    Sample = 'uses: softprops/action-gh-release@v2' },
        @{ Expected = 'docker\s+push';        Sample = 'run: docker push hibiki/image' },
        @{ Expected = 'npm\s+publish';        Sample = 'run: npm publish' },
        @{ Expected = 'nuget\s+push';         Sample = 'run: nuget push pkg.nupkg' },
        @{ Expected = 'packages:\s*write';    Sample = 'permissions:`n  packages: write' },
        @{ Expected = 'id-token:\s*write';    Sample = 'permissions:`n  id-token: write' },
        @{ Expected = 'GUMROAD';              Sample = 'env:`n  GUMROAD_TOKEN: secret' },
        @{ Expected = 'PARTNER_CENTER';       Sample = 'env:`n  PARTNER_CENTER_ID: x' }
    )
    foreach ($entry in $patternSamples) {
        $expectedLiteral = [regex]::Escape($entry.Expected)
        Assert-GateRejection -Label "forbidden-pattern $($entry.Expected)" -ExpectedPattern $expectedLiteral `
            -Action { Test-WorkflowText -Name 'evil.yml' -Text ("`n" + $entry.Sample + "`n") }
        $caseCount++
    }

    # Case 12: a repository whose workflows never reference source-policy is rejected.
    Assert-GateRejection -Label 'missing-source-policy-reference' -ExpectedPattern 'A workflow must run tools/source-policy\.ps1' `
        -Action { Assert-WorkflowPolicyReferencePresent -Any $false }
    $caseCount++

    # Case 13: at least one referencing workflow satisfies the requirement across many files.
    Assert-WorkflowPolicyReferencePresent -Any $true
    $caseCount++

    # Case 14: every blocked binary extension class is flagged, case-insensitively.
    $binarySamples = @('tools/a.exe', 'lib/b.dll', 'drv/c.sys', 'pkg/d.msi', 'store/e.msix', 'fx/f.vst3',
        'g.cat', 'h.pdb', 'i.obj', 'j.lib', 'k.pfx', 'l.key', 'm.pem', 'n.cab', 'o.zip', 'p.7z',
        'q.bin', 'r.so', 's.dylib')
    $flagged = @(Get-BlockedTrackedPaths -Paths ($binarySamples + @('UPPER/X.EXE')))
    if (@($flagged).Count -ne @($binarySamples).Count + 1) {
        throw ("source-only-ci-check self-test case 'blocked-binary-extensions' flagged {0} paths but expected {1}.") -f @($flagged).Count, @($binarySamples).Count + 1
    }
    $caseCount++

    # Case 15: multiple violations are reported together in input order.
    $joined = @(Get-BlockedTrackedPaths -Paths @('z.exe', 'keep.md', 'a.dll'))
    if (@($joined).Count -ne 2 -or $joined[0] -ne 'z.exe' -or $joined[1] -ne 'a.dll') {
        throw "source-only-ci-check self-test case 'violation-join' returned an unexpected result: $($joined -join ', ')."
    }
    $caseCount++

    # Case 16: allowed text paths produce no rejections.
    $allowed = @(Get-BlockedTrackedPaths -Paths @('README.md', 'tools/tool.ps1', 'schemas/schema.json', 'docs/guide.yml'))
    if (@($allowed).Count -ne 0) { throw "source-only-ci-check self-test case 'allowed-text-paths' flagged: $($allowed -join ', ')." }
    $caseCount++

    # Case 17: the canonical verify tag trigger and tag-scoped provenance gate pass together.
    $verifyTagWorkflow = @'
name: verify
on:
  push:
    branches: [main]
    tags: ['v*']
jobs:
  verify:
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-VerifyTagProvenanceWiring -Text $verifyTagWorkflow
    $caseCount++

    # Case 18: removing tag triggers is rejected even when the gate step remains.
    Assert-GateRejection -Label 'missing-verify-tag-trigger' -ExpectedPattern 'must trigger on v\* source tags' `
        -Action { Assert-VerifyTagProvenanceWiring -Text ($verifyTagWorkflow -replace "(?m)^\s*tags:.*\r?\n", '') }
    $caseCount++

    # Case 19: a generic provenance command without the tag event guard is rejected.
    Assert-GateRejection -Label 'missing-tag-scoped-provenance-gate' -ExpectedPattern 'must include the tag-scoped Source-tag provenance gate' `
        -Action { Assert-VerifyTagProvenanceWiring -Text ($verifyTagWorkflow -replace "if: github\.ref_type == 'tag'", 'if: github.ref_type == ''branch''') }
    $caseCount++

    # Case 20: a gate in another job or inside comments cannot spoof jobs.verify.steps.
    $spoofedWorkflow = @'
name: verify
on:
  push:
    branches: [main]
    tags: ['v*']
jobs:
  other:
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
  verify:
    steps:
      # - name: Source-tag provenance gate
      #   if: github.ref_type == 'tag'
      #   shell: pwsh
      #   run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-GateRejection -Label 'spoofed-verify-wiring' -ExpectedPattern 'must include the tag-scoped Source-tag provenance gate' `
        -Action { Assert-VerifyTagProvenanceWiring -Text $spoofedWorkflow }
    $caseCount++

    # Case 21: executable-looking text inside a YAML literal is not workflow wiring.
    $literalSpoofWorkflow = @'
name: verify
on:
  push:
    branches: [main]
jobs:
  verify:
    steps:
      - name: harmless
        run: |
          tags: ['v*']
          - name: Source-tag provenance gate
            if: github.ref_type == 'tag'
            shell: pwsh
            run: ./tools/release-provenance-check.ps1 -Tag $env:GITHUB_REF_NAME
'@
    Assert-GateRejection -Label 'literal-scalar-wiring-spoof' -ExpectedPattern ([regex]::Escape('must trigger on v* source tags')) -Action { Assert-VerifyTagProvenanceWiring -Text $literalSpoofWorkflow }
    $caseCount++

    # Case 22: a job-level condition cannot disable the required step-level gate.
    $jobLevelGuardWorkflow = @'
name: verify
on:
  push:
    tags: ['v*']
jobs:
  verify:
    if: false
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-GateRejection -Label 'job-level-tag-guard' -ExpectedPattern 'jobs\.verify cannot use a job-level if' -Action { Assert-VerifyTagProvenanceWiring -Text $jobLevelGuardWorkflow }
    $caseCount++

    # Case 23: a tags key outside on.push cannot satisfy the source-tag contract.
    $misplacedTagWorkflow = @'
name: verify
on:
  push:
    branches: [main]
  workflow_dispatch:
    tags: ['v*']
jobs:
  verify:
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-GateRejection -Label 'misplaced-tag-selector' -ExpectedPattern ([regex]::Escape('must trigger on v* source tags')) -Action { Assert-VerifyTagProvenanceWiring -Text $misplacedTagWorkflow }
    $caseCount++

    # Case 24: the provenance step may not convert a failing gate into a success.
    $continueOnErrorWorkflow = @'
name: verify
on:
  push:
    tags: ['v*']
jobs:
  verify:
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
        continue-on-error: true
'@
    Assert-GateRejection -Label 'step-continue-on-error' -ExpectedPattern 'Source-tag provenance gate cannot set continue-on-error' -Action { Assert-VerifyTagProvenanceWiring -Text $continueOnErrorWorkflow }
    $caseCount++

    # Case 25: a skipped dependency must not prevent the provenance job from running.
    $needsWorkflow = @'
name: verify
on:
  push:
    tags: ['v*']
jobs:
  blocker:
    if: false
    steps:
      - run: echo skipped
  verify:
    needs: blocker
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-GateRejection -Label 'job-needs-dependency' -ExpectedPattern 'jobs\.verify cannot use needs' -Action { Assert-VerifyTagProvenanceWiring -Text $needsWorkflow }
    $caseCount++

    # Case 26: a matrix strategy must not turn the provenance job into zero jobs.
    $strategyWorkflow = @'
name: verify
on:
  push:
    tags: ['v*']
jobs:
  verify:
    strategy:
      matrix:
        include: []
    steps:
      - name: Source-tag provenance gate
        if: github.ref_type == 'tag'
        shell: pwsh
        run: '& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1" -Tag $env:GITHUB_REF_NAME'
'@
    Assert-GateRejection -Label 'job-matrix-strategy' -ExpectedPattern 'jobs\.verify cannot use strategy' -Action { Assert-VerifyTagProvenanceWiring -Text $strategyWorkflow }
    $caseCount++

    # Case 27: the provenance command is anchored at the checked-out workspace.
    $relativeRunWorkflow = $verifyTagWorkflow.Replace('& "$env:GITHUB_WORKSPACE/tools/release-provenance-check.ps1"', './tools/release-provenance-check.ps1')
    Assert-GateRejection -Label 'relative-provenance-command' -ExpectedPattern 'must include the tag-scoped Source-tag provenance gate' -Action { Assert-VerifyTagProvenanceWiring -Text $relativeRunWorkflow }
    $caseCount++

    Write-Output "Source-only CI publication gate self-test passed ($caseCount cases)."
    exit 0
}

$workflowRoot = Join-Path $repo '.github/workflows'
if (-not (Test-Path -LiteralPath $workflowRoot)) { throw 'Missing GitHub workflow directory.' }

$workflows = @(Get-ChildItem -LiteralPath $workflowRoot -File -Include '*.yml', '*.yaml')
if (@($workflows).Count -eq 0) { throw 'No GitHub workflow is available for the source-only gate.' }

$hasSourcePolicy = $false
foreach ($workflow in $workflows) {
    $text = Get-Content -LiteralPath $workflow.FullName -Raw
    $referencesPolicy = Test-WorkflowText -Name $workflow.Name -Text $text
    if ($referencesPolicy) { $hasSourcePolicy = $true }
}
Assert-WorkflowPolicyReferencePresent -Any $hasSourcePolicy

$verifyWorkflow = Join-Path $workflowRoot 'verify.yml'
if (-not (Test-Path -LiteralPath $verifyWorkflow)) { throw 'Missing verify.yml workflow.' }
Assert-VerifyTagProvenanceWiring -Text (Get-Content -LiteralPath $verifyWorkflow -Raw)

$tracked = @(git -C $repo ls-files 2>$null)
$blocked = @(Get-BlockedTrackedPaths -Paths $tracked)
if (@($blocked).Count -gt 0) { throw "Source-only CI sees tracked binary: $($blocked -join ', ')" }

Write-Output "Source-only CI policy passed ($(@($workflows).Count) workflows, $(@($tracked).Count) tracked paths)."
