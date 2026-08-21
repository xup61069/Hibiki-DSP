[CmdletBinding()]
param(
  [Parameter(Mandatory = $false)]
  [string]$PackageRoot,
  [switch]$RequireInf2Cat,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function Get-InfSections([string]$text) {
  $sections = @{}
  $current = $null
  foreach ($line in ($text -split "`r?`n")) {
    $trimmed = $line.Trim()
    if (-not $trimmed -or $trimmed.StartsWith(';')) { continue }
    $header = [regex]::Match($trimmed, '^\[(?<name>[^\]]+)\]\s*$')
    if ($header.Success) {
      $current = $header.Groups['name'].Value
      if (-not $sections.ContainsKey($current)) { $sections[$current] = @() }
      continue
    }
    if ($null -eq $current) { continue }
    $active = ($trimmed -split ';', 2)[0].Trim()
    if ($active) { $sections[$current] += $active }
  }
  return ,$sections
}

function Assert-InfDirective($sections, [string]$section, [string]$pattern, [string]$label, [string]$sourceName) {
  if (-not $sections.ContainsKey($section)) {
    throw "INF section [$section] is missing for $($label): $sourceName"
  }
  $found = $false
  foreach ($line in @($sections[$section])) {
    if ($line -match $pattern) { $found = $true; break }
  }
  if (-not $found) { throw "INF section [$section] is missing $($label): $sourceName" }
}

function Assert-InfSourcePolicy([string]$infText, [string]$sourceName) {
  $sections = Get-InfSections $infText
  Assert-InfDirective $sections 'Version' '^Signature\s*=\s*"\$WINDOWS NT\$"\s*$' 'Signature="$WINDOWS NT$"' $sourceName
  Assert-InfDirective $sections 'Version' '^Class\s*=\s*MEDIA\s*$' 'Class=MEDIA' $sourceName
  Assert-InfDirective $sections 'Version' '^CatalogFile\s*=\s*HibikiVirtualAudio\.cat\s*$' 'CatalogFile=HibikiVirtualAudio.cat' $sourceName
  Assert-InfDirective $sections 'Version' '^PnpLockdown\s*=\s*1\s*$' 'PnpLockdown=1' $sourceName
  Assert-InfDirective $sections 'Manufacturer' '^%HibikiProvider%\s*=\s*Hibiki\s*,\s*NTamd64\s*$' 'NTamd64 manufacturer mapping' $sourceName
  Assert-InfDirective $sections 'Hibiki.NTamd64' '=.*Root\\HibikiDSP\s*$' 'Root\\HibikiDSP install mapping' $sourceName
  Assert-InfDirective $sections 'HibikiMain_Install' '^Include\s*=\s*ks\.inf\s*,\s*wdmaudio\.inf\s*$' 'KS/WDMAUDIO Include' $sourceName
  Assert-InfDirective $sections 'HibikiMain_Install' '^Needs\s*=\s*KS\.Registration\s*,\s*WDMAUDIO\.Registration\s*$' 'KS/WDMAUDIO Needs' $sourceName
  Assert-InfDirective $sections 'HibikiMain_Install' '^CopyFiles\s*=\s*HibikiDriver\.CopyFiles\s*$' 'driver CopyFiles binding' $sourceName
  Assert-InfDirective $sections 'HibikiMain_Install.Services' '^AddService\s*=\s*HibikiVirtualAudio\s*,\s*0x00000002\s*,\s*HibikiVirtualAudio\.Service\s*$' 'driver AddService binding' $sourceName
  Assert-InfDirective $sections 'HibikiDriver.CopyFiles' '^HibikiVirtualAudio\.sys\s*$' 'SYS copy entry' $sourceName
  Assert-InfDirective $sections 'HibikiVirtualAudio.Service' '^ServiceBinary\s*=\s*%12%\\HibikiVirtualAudio\.sys\s*$' 'ServiceBinary' $sourceName
  Assert-InfDirective $sections 'SourceDisksFiles' '^HibikiVirtualAudio\.sys\s*=\s*1\s*$' 'source disk SYS entry' $sourceName

  $activeText = ($sections.Values | ForEach-Object { $_ }) -join "`n"
  if ($activeText -match '(?i)GUMROAD|PRIVATE KEY|HibikiDSP\.dll|(^|[=,])\s*\.\.[\\/]|[A-Za-z]:[\\/]') {
    throw "INF contains forbidden credential, GPL payload, or traversal content: $sourceName"
  }
}

if ($SelfTest) {
  $valid = @'
[Version]
Signature="$WINDOWS NT$"
Class=MEDIA
CatalogFile=HibikiVirtualAudio.cat
PnpLockdown=1
[Manufacturer]
%HibikiProvider%=Hibiki,NTamd64
[Hibiki.NTamd64]
%HibikiMain.DeviceDesc%=HibikiMain_Install,Root\HibikiDSP
[HibikiMain_Install]
Include=ks.inf,wdmaudio.inf
Needs=KS.Registration,WDMAUDIO.Registration
CopyFiles=HibikiDriver.CopyFiles
[HibikiMain_Install.Services]
AddService=HibikiVirtualAudio,0x00000002,HibikiVirtualAudio.Service
[HibikiDriver.CopyFiles]
HibikiVirtualAudio.sys
[HibikiVirtualAudio.Service]
ServiceBinary=%12%\HibikiVirtualAudio.sys
[SourceDisksFiles]
HibikiVirtualAudio.sys=1
'@
  Assert-InfSourcePolicy $valid 'selftest-valid.inf'

  foreach ($fixture in @(
      @{ Name = 'missing-section-directive'; Text = $valid.Replace('CatalogFile=HibikiVirtualAudio.cat', '') },
      @{ Name = 'misplaced-directive'; Text = $valid.Replace('PnpLockdown=1', '') + "`nPnpLockdown=1" },
      @{ Name = 'comment-only-token'; Text = $valid.Replace('CatalogFile=HibikiVirtualAudio.cat', '; CatalogFile=HibikiVirtualAudio.cat') },
      @{ Name = 'forbidden-traversal'; Text = $valid + "`n[Bad]`nCopyFiles=..\secret.sys" }
  )) {
    $caught = $false
    try { Assert-InfSourcePolicy $fixture.Text "selftest-$($fixture.Name).inf" } catch { $caught = $true }
    if (-not $caught) { throw "INF section self-test expected rejection: $($fixture.Name)" }
  }
  Write-Output 'Driver INF section self-test passed (5 cases).'
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$inf = Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf'
if (-not (Test-Path -LiteralPath $inf)) { throw 'Missing source-only HibikiVirtualAudio.inf.' }
$infText = Get-Content -LiteralPath $inf -Raw
Assert-InfSourcePolicy $infText $inf
foreach ($required in @('Signature="$WINDOWS NT$"', 'Class=MEDIA', 'PnpLockdown=1',
    'Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'CatalogFile=HibikiVirtualAudio.cat')) {
  if (-not $infText.Contains($required)) { throw "INF source missing signability boundary: $required" }
}

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
  Write-Output 'Driver signability source check passed (no package supplied; Inf2Cat not run).'
  if ($RequireInf2Cat) { throw '-RequireInf2Cat requires -PackageRoot with a built SYS package.' }
  exit 0
}

$package = (Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
$packageInf = Join-Path $package 'HibikiVirtualAudio.inf'
$packageSys = Join-Path $package 'HibikiVirtualAudio.sys'
if (-not (Test-Path -LiteralPath $packageInf) -or -not (Test-Path -LiteralPath $packageSys)) {
  throw 'PackageRoot must contain HibikiVirtualAudio.inf and HibikiVirtualAudio.sys.'
}
$packageInfText = Get-Content -LiteralPath $packageInf -Raw
Assert-InfSourcePolicy $packageInfText $packageInf
foreach ($required in @('Root\HibikiDSP', 'HibikiVirtualAudio.sys', 'PnpLockdown=1')) {
  if (-not $packageInfText.Contains($required)) { throw "Built INF missing stable value: $required" }
}

$inf2cat = $null
if ($env:WDK_BIN) {
  $candidate = Join-Path $env:WDK_BIN 'Inf2Cat.exe'
  if (Test-Path -LiteralPath $candidate) { $inf2cat = $candidate }
}
if ($null -eq $inf2cat) {
  $command = Get-Command Inf2Cat.exe -ErrorAction SilentlyContinue
  if ($null -ne $command) { $inf2cat = $command.Source }
}
if ($null -eq $inf2cat) {
  throw 'Inf2Cat.exe not found; install the locked WDK or set WDK_BIN.'
}

& $inf2cat "/driver:$package" '/os:10_GE_X64,10_25H2_X64' '/verbose'
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE." }
$catalog = Join-Path $package 'HibikiVirtualAudio.cat'
if (-not (Test-Path -LiteralPath $catalog)) { throw 'Inf2Cat did not produce HibikiVirtualAudio.cat.' }
Write-Output 'Driver signability check passed for Windows 11 24H2/25H2 x64.'
