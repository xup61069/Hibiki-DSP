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

$tracked = @(git -C $repo ls-files 2>$null)
$blocked = @(Get-BlockedTrackedPaths -Paths $tracked)
if (@($blocked).Count -gt 0) { throw "Source-only CI sees tracked binary: $($blocked -join ', ')" }

Write-Output "Source-only CI policy passed ($(@($workflows).Count) workflows, $(@($tracked).Count) tracked paths)."
