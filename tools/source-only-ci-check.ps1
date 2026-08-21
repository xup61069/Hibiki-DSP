[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$workflowRoot = Join-Path $repo '.github/workflows'
if (-not (Test-Path -LiteralPath $workflowRoot)) { throw 'Missing GitHub workflow directory.' }

$workflows = @(Get-ChildItem -LiteralPath $workflowRoot -File -Include '*.yml', '*.yaml')
if ($workflows.Count -eq 0) { throw 'No GitHub workflow is available for the source-only gate.' }

$blockedPatterns = @(
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
$hasSourcePolicy = $false
foreach ($workflow in $workflows) {
    $text = Get-Content -LiteralPath $workflow.FullName -Raw
    if ($text -match '(?i)tools/source-policy\.ps1') { $hasSourcePolicy = $true }
    foreach ($pattern in $blockedPatterns) {
        if ($text -match "(?i)$pattern") {
            throw "Source-only CI violation '$pattern' in $($workflow.Name)."
        }
    }
}
if (-not $hasSourcePolicy) { throw 'A workflow must run tools/source-policy.ps1.' }

$tracked = @(git -C $repo ls-files 2>$null)
$blockedExtensions = @('.exe', '.dll', '.sys', '.msi', '.msix', '.vst3', '.cat', '.pdb', '.obj',
    '.lib', '.pfx', '.key', '.pem', '.cab', '.zip', '.7z', '.bin', '.so', '.dylib')
$blocked = @($tracked | Where-Object {
    $blockedExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant()
})
if ($blocked.Count -gt 0) { throw "Source-only CI sees tracked binary: $($blocked -join ', ')" }

Write-Output "Source-only CI policy passed ($($workflows.Count) workflows, $($tracked.Count) tracked paths)."
