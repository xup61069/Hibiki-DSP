[CmdletBinding()]
param(
  [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$results = [System.Collections.Generic.List[object]]::new()

function Add-Check([string]$Name, [bool]$Ok, [string]$Detail) {
  $results.Add([pscustomobject]@{ Name = $Name; Status = $(if ($Ok) { 'OK' } else { 'MISSING' }); Detail = $Detail })
}

$os = Get-CimInstance Win32_OperatingSystem
$build = [int]$os.BuildNumber
Add-Check 'Windows build' ($build -ge 26100) "$($os.Caption) build $build (minimum 26100)"
Add-Check 'Architecture' ([Environment]::Is64BitOperatingSystem) "$env:PROCESSOR_ARCHITECTURE"

foreach ($tool in @('git', 'cmake', 'pwsh')) {
  $command = Get-Command $tool -ErrorAction SilentlyContinue
  Add-Check $tool ($null -ne $command) $(if ($command) { $command.Source } else { 'Install or expose this tool on PATH' })
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsVersion = $null
if (Test-Path $vswhere) {
  $vsVersion = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>$null | Select-Object -First 1)
}
$vsMajor = 0
if ($vsVersion -match '^([0-9]+)') { $vsMajor = [int]$Matches[1] }
Add-Check ($('Visual Studio 2026')) ($vsMajor -ge 18) $(if ($vsVersion) { "detected $vsVersion; required major 18" } else { 'Install Visual Studio 2026 with the C++ workload' })

$sdkRoots = @(
  (Join-Path $env:ProgramFiles 'Windows Kits/10/Include'),
  (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include')
)
$requiredSdk = $sdkRoots | ForEach-Object { Join-Path $_ '10.0.28000.2526' } | Where-Object { Test-Path $_ } | Select-Object -First 1
Add-Check 'Windows SDK/WDK 10.0.28000.2526' ($null -ne $requiredSdk) $(if ($requiredSdk) { $requiredSdk } else { 'Install matching SDK/WDK 10.0.28000.2526 for driver work' })

$results | Format-Table -AutoSize
if (($results | Where-Object Status -eq 'MISSING').Count -gt 0) {
  Write-Warning 'The repository foundation can be inspected now, but a full Windows build requires the missing prerequisites.'
  if (-not $CheckOnly) { exit 2 }
}
