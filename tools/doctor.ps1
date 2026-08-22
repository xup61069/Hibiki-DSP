[CmdletBinding()]
param(
  [switch]$CheckOnly,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$results = [System.Collections.Generic.List[object]]::new()

$requiredKitVersion = '10.0.28000.2526'
$kitDirectoryVersion = '10.0.28000.0'
$requiredPackageVersion = '10.1.28000.2526'

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

  $Entries |
    Where-Object {
      $_.DisplayVersion -eq $requiredPackageVersion -and
      $_.DisplayName -match $namePattern
    } |
    Select-Object -First 1
}

function Get-WindowsKitAssessment(
  [object[]]$Candidates,
  [object[]]$PackageEntries
) {
  $candidate = $Candidates |
    Where-Object { $_.IncludeExists -and $_.BuildExists -and $_.ToolsExists } |
    Select-Object -First 1
  $sdkPackage = Find-WindowsKitPackage -Entries $PackageEntries -Kind Sdk
  $wdkPackage = Find-WindowsKitPackage -Entries $PackageEntries -Kind Wdk
  $reasons = [System.Collections.Generic.List[string]]::new()

  if ($null -eq $candidate) {
    $reasons.Add("missing Include/build/Tools directories for $kitDirectoryVersion")
  }
  if ($null -eq $sdkPackage) {
    $reasons.Add("missing Windows SDK package metadata version $requiredPackageVersion")
  }
  if ($null -eq $wdkPackage) {
    $reasons.Add("missing Windows Driver Kit package metadata version $requiredPackageVersion")
  }

  $ok = $reasons.Count -eq 0
  $detail = if ($ok) {
    "on-disk=$kitDirectoryVersion; SDK QFE=$($sdkPackage.DisplayVersion); WDK QFE=$($wdkPackage.DisplayVersion); root=$($candidate.Root)"
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
  $exactPackages = @(
    [pscustomobject]@{ DisplayName = 'Windows Software Development Kit - Windows 10.0.28000.2526'; DisplayVersion = '10.1.28000.2526' },
    [pscustomobject]@{ DisplayName = 'Windows Driver Kit - Windows 10.0.28000.2526'; DisplayVersion = '10.1.28000.2526' }
  )
  $exactCandidate = [pscustomobject]@{
    Root = 'fixture/windows-kits/10'
    IncludeExists = $true
    BuildExists = $true
    ToolsExists = $true
  }
  $cases = @(
    @{ Name = 'exact-match'; Candidates = @($exactCandidate); Packages = $exactPackages; Expected = $true },
    @{ Name = 'missing-sdk-metadata'; Candidates = @($exactCandidate); Packages = @($exactPackages | Where-Object { $_.DisplayName -notmatch 'Software Development Kit' }); Expected = $false },
    @{ Name = 'mismatched-qfe'; Candidates = @($exactCandidate); Packages = @($exactPackages | ForEach-Object { [pscustomobject]@{ DisplayName = $_.DisplayName; DisplayVersion = '10.1.26100.8249' } }); Expected = $false },
    @{ Name = 'missing-kit-directory'; Candidates = @([pscustomobject]@{ Root = 'fixture/windows-kits/10'; IncludeExists = $false; BuildExists = $true; ToolsExists = $true }); Packages = $exactPackages; Expected = $false }
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
  [pscustomobject]@{
    Root = $root
    IncludeExists = Test-Path (Join-Path $root "Include/$kitDirectoryVersion")
    BuildExists = Test-Path (Join-Path $root "build/$kitDirectoryVersion")
    ToolsExists = Test-Path (Join-Path $root "Tools/$kitDirectoryVersion")
  }
}
$kitAssessment = Get-WindowsKitAssessment -Candidates $kitCandidates -PackageEntries (Get-WindowsKitPackageEntries)
Add-Check "Windows SDK/WDK $requiredKitVersion" $kitAssessment.Ok $kitAssessment.Detail

$results | Format-Table -AutoSize
if (($results | Where-Object Status -eq 'MISSING').Count -gt 0) {
  Write-Warning 'The repository foundation can be inspected now, but a full Windows build requires the missing prerequisites.'
  if (-not $CheckOnly) { exit 2 }
}
