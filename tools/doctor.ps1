[CmdletBinding()]
param(
  [switch]$CheckOnly,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$results = [System.Collections.Generic.List[object]]::new()

$minimumKitVersion = '10.0.26100'
$kitFamilyPrefix = '10.1.26100.'

function Add-Check([string]$Name, [bool]$Ok, [string]$Detail) {
  $results.Add([pscustomobject]@{ Name = $Name; Status = $(if ($Ok) { 'OK' } else { 'MISSING' }); Detail = $Detail })
}

function Get-WindowsKitPackageEntries {
  $uninstallRoots = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
  )

  foreach ($root in $uninstallRoots) {
    Get-ItemProperty -Path $root -ErrorAction SilentlyContinue |
      Where-Object { $_.DisplayName -and $_.DisplayVersion } |
      Select-Object DisplayName, DisplayVersion
  }
}

function Test-DisplayVersionAtLeast {
  param([string]$Version, [version]$Minimum)
  try { return [version]$Version -ge $Minimum } catch { return $false }
}

function Find-WindowsKitPackage(
  [object[]]$Entries,
  [ValidateSet('Sdk', 'Wdk')]
  [string]$Kind
) {
  $namePattern = if ($Kind -eq 'Sdk') {
    'Windows (Software )?Development Kit|Windows SDK'
  } else {
    'Windows Driver Kit'
  }
  $floor = [version]'10.1.26100.0'

  $Entries |
    Where-Object {
      (Test-DisplayVersionAtLeast -Version ([string]$_.DisplayVersion) -Minimum $floor) -and
      $_.DisplayName -match $namePattern
    } |
    Select-Object -First 1
}

function Test-WindowsKitDirectoryVersion {
  param([string]$Version)
  $parts = $Version.Split('.')
  if ($parts.Count -lt 3) { return $false }
  try { return ([version]"$($parts[0]).$($parts[1]).$($parts[2])") -ge [version]$minimumKitVersion } catch { return $false }
}

function Get-WindowsKitAssessment(
  [object[]]$Candidates,
  [object[]]$PackageEntries
) {
  $candidate = $Candidates |
    Where-Object { $_.IncludeExists -and $_.BuildExists -and $_.ToolsExists -and (Test-WindowsKitDirectoryVersion -Version $_.Version) } |
    Select-Object -First 1
  $sdkPackage = Find-WindowsKitPackage -Entries $PackageEntries -Kind Sdk
  $wdkPackage = Find-WindowsKitPackage -Entries $PackageEntries -Kind Wdk
  $reasons = [System.Collections.Generic.List[string]]::new()

  if ($null -eq $candidate) {
    $reasons.Add("missing Include/build/Tools directories for minimum kit version $minimumKitVersion")
  }
  if ($null -eq $sdkPackage) {
    $reasons.Add("missing Windows SDK package metadata at or above 10.1.26100.0")
  }
  if ($null -eq $wdkPackage) {
    $reasons.Add("missing Windows Driver Kit package metadata at or above 10.1.26100.0")
  }

  $ok = $reasons.Count -eq 0
  $detail = if ($ok) {
    "on-disk=$($candidate.Version); SDK QFE=$($sdkPackage.DisplayVersion); WDK QFE=$($wdkPackage.DisplayVersion); root=$($candidate.Root)"
  } else {
    $reasons -join '; '
  }

  [pscustomobject]@{
    Ok = $ok
    Detail = $detail
    Root = if ($candidate) { $candidate.Root } else { $null }
    SdkPackageVersion = if ($sdkPackage) { $sdkPackage.DisplayVersion } else { $null }
    WdkPackageVersion = if ($wdkPackage) { $wdkPackage.DisplayVersion } else { $null }
  }
}

function Invoke-WindowsKitSelfTest {
  $familyPackages = @(
    [pscustomobject]@{ DisplayName = 'Windows Software Development Kit - Windows 10.0.26100'; DisplayVersion = '10.1.26100.8249' },
    [pscustomobject]@{ DisplayName = 'Windows Driver Kit - Windows 10.0.26100'; DisplayVersion = '10.1.26100.6584' }
  )
  $exactCandidate = [pscustomobject]@{
    Root = 'fixture/windows-kits/10'
    Version = '10.0.26100.0'
    IncludeExists = $true
    BuildExists = $true
    ToolsExists = $true
  }
  $cases = @(
    @{ Name = 'family-match'; Candidates = @($exactCandidate); Packages = $familyPackages; Expected = $true },
    @{ Name = 'missing-sdk-metadata'; Candidates = @($exactCandidate); Packages = @($familyPackages | Where-Object { $_.DisplayName -notmatch 'Software Development Kit' }); Expected = $false },
    @{ Name = 'newer-family-package'; Candidates = @($exactCandidate); Packages = @($familyPackages | ForEach-Object { [pscustomobject]@{ DisplayName = $_.DisplayName; DisplayVersion = '10.1.28000.2526' } }); Expected = $true },
    @{ Name = 'below-floor-package'; Candidates = @($exactCandidate); Packages = @($familyPackages | ForEach-Object { [pscustomobject]@{ DisplayName = $_.DisplayName; DisplayVersion = '10.1.15063.468' } }); Expected = $false },
    @{ Name = 'below-minimum-directory'; Candidates = @([pscustomobject]@{ Root = 'fixture/windows-kits/10'; IncludeExists = $true; BuildExists = $true; ToolsExists = $true; Version = '10.0.22621.0' }); Packages = $familyPackages; Expected = $false },
    @{ Name = 'missing-kit-directory'; Candidates = @([pscustomobject]@{ Root = 'fixture/windows-kits/10'; IncludeExists = $false; BuildExists = $true; ToolsExists = $true; Version = '10.0.26100.0' }); Packages = $familyPackages; Expected = $false }
  )

  foreach ($case in $cases) {
    $assessment = Get-WindowsKitAssessment -Candidates $case.Candidates -PackageEntries $case.Packages
    if ($assessment.Ok -ne $case.Expected) {
      throw "Windows SDK/WDK detection self-test failed: $($case.Name) expected $($case.Expected), got $($assessment.Ok)."
    }
  }

  Write-Output "Windows SDK/WDK detection self-test passed ($($cases.Count) cases)."
}

if ($SelfTest) {
  Invoke-WindowsKitSelfTest
  exit 0
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

$kitRoots = @(
  (Join-Path $env:ProgramFiles 'Windows Kits/10'),
  (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10')
) | Select-Object -Unique
$kitCandidates = foreach ($root in $kitRoots) {
  foreach ($dir in @(Get-ChildItem -LiteralPath (Join-Path $root 'Include') -Directory -ErrorAction SilentlyContinue)) {
    $versionName = $dir.Name
    if ($versionName -notmatch '^10\.0\.\d+(\.0)?$') { continue }
    [pscustomobject]@{
      Root = $root
      Version = $versionName
      IncludeExists = Test-Path (Join-Path $root "Include/$versionName")
      BuildExists = Test-Path (Join-Path $root "build/$versionName")
      ToolsExists = Test-Path (Join-Path $root "Tools/$versionName")
    }
  }
}
$kitAssessment = Get-WindowsKitAssessment -Candidates $kitCandidates -PackageEntries (Get-WindowsKitPackageEntries)
Add-Check "Windows SDK/WDK >= $minimumKitVersion" $kitAssessment.Ok $kitAssessment.Detail

$results | Format-Table -AutoSize
if (($results | Where-Object Status -eq 'MISSING').Count -gt 0) {
  Write-Warning 'The repository foundation can be inspected now, but a full Windows build requires the missing prerequisites.'
  if (-not $CheckOnly) { exit 2 }
}
