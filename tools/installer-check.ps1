[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'installer/HibikiSetup.ps1'
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { throw "Installer PowerShell parse errors: $($errors -join '; ')" }
$text = Get-Content -LiteralPath $path -Raw
foreach ($required in @('Read-ReleaseManifest', 'Test-ManifestFiles', 'Invoke-HibikiInstall', '-Apply', 'pnputil.exe')) {
  if (-not $text.Contains($required)) { throw "Installer source missing required boundary: $required" }
}
Write-Output 'Installer source checks passed.'
