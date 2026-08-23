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

function Find-KitsInf2Cat([string]$KitsRoot) {
  if ([string]::IsNullOrWhiteSpace($KitsRoot)) { return $null }
  if (-not (Test-Path -LiteralPath $KitsRoot)) { return $null }
  return Get-ChildItem -LiteralPath $KitsRoot -Recurse -Filter Inf2Cat.exe -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
}

function Resolve-Inf2Cat {
  param(
    [AllowEmptyString()][string]$WdkBin,
    [AllowEmptyString()][string]$KitsRoot,
    [scriptblock]$GetCommandSource
  )
  if (-not [string]::IsNullOrWhiteSpace($WdkBin)) {
    $candidate = Join-Path $WdkBin 'Inf2Cat.exe'
    if (Test-Path -LiteralPath $candidate) { return $candidate }
  }
  $kitCandidate = Find-KitsInf2Cat -KitsRoot $KitsRoot
  if ($kitCandidate) { return $kitCandidate }
  if ($GetCommandSource) {
    $pathCandidate = @(& $GetCommandSource) | Select-Object -First 1
    if ($pathCandidate) { return $pathCandidate }
  }
  return $null
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
      @{ Name = 'forbidden-traversal'; Text = $valid + "`n[Bad]`nCopyFiles=..\secret.sys" },
      @{ Name = 'missing-service-binding'; Text = $valid.Replace('AddService=HibikiVirtualAudio,0x00000002,HibikiVirtualAudio.Service', '') },
      @{ Name = 'forbidden-gpl-payload'; Text = $valid + "`n[Bad]`nCopyFiles=HibikiDSP.dll" },
      @{ Name = 'forbidden-private-key-token'; Text = $valid + "`n[Bad]`nDescription=PRIVATE KEY" },
      @{ Name = 'forbidden-absolute-drive-path'; Text = $valid + "`n[Bad]`nCopyFiles=C:\secret.sys" }
  )) {
    $caught = $false
    try { Assert-InfSourcePolicy $fixture.Text "selftest-$($fixture.Name).inf" } catch { $caught = $true }
    if (-not $caught) { throw "INF section self-test expected rejection: $($fixture.Name)" }
  }
  $resolverRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('hibiki-signability-selftest-' + [guid]::NewGuid().ToString('N'))
  try {
    $wdkBinDir = Join-Path $resolverRoot 'wdk-bin'
    $kitsBinDir = Join-Path (Join-Path $resolverRoot 'kits') 'bin'
    $pathToolDir = Join-Path $resolverRoot 'path-tools'
    foreach ($dir in @($wdkBinDir, $kitsBinDir, $pathToolDir)) {
      New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $overrideTool = Join-Path $wdkBinDir 'Inf2Cat.exe'
    $kitTreeTool = Join-Path (Join-Path (Join-Path $kitsBinDir '10.0.99999.0') 'x64') 'Inf2Cat.exe'
    $pathTool = Join-Path $pathToolDir 'Inf2Cat.exe'
    New-Item -ItemType Directory -Path (Split-Path -Parent $kitTreeTool) -Force | Out-Null
    foreach ($tool in @($overrideTool, $kitTreeTool, $pathTool)) {
      Set-Content -LiteralPath $tool -Value 'selftest stub' -NoNewline
    }

    $pathLookup = { $pathTool }
    $resolvedOverride = Resolve-Inf2Cat -WdkBin $wdkBinDir -KitsRoot $kitsBinDir -GetCommandSource $pathLookup
    if ($resolvedOverride -ne $overrideTool) { throw 'signability self-test failed: WDK_BIN override did not win.' }
    $caseCount++

    $missingOverrideDir = Join-Path $resolverRoot 'missing-wdk-bin'
    $resolvedKitTree = Resolve-Inf2Cat -WdkBin $missingOverrideDir -KitsRoot $kitsBinDir -GetCommandSource $pathLookup
    if ($resolvedKitTree -ne $kitTreeTool) { throw 'signability self-test failed: kit-tree search did not beat PATH.' }
    $caseCount++

    $missingKitsDir = Join-Path $resolverRoot 'missing-kits'
    $resolvedPath = Resolve-Inf2Cat -WdkBin $missingOverrideDir -KitsRoot $missingKitsDir -GetCommandSource $pathLookup
    if ($resolvedPath -ne $pathTool) { throw 'signability self-test failed: PATH fallback was skipped.' }
    $caseCount++

    $emptyLookup = { $null }
    $resolvedMissing = Resolve-Inf2Cat -WdkBin $missingOverrideDir -KitsRoot $missingKitsDir -GetCommandSource $emptyLookup
    if ($null -ne $resolvedMissing) { throw 'signability self-test failed: all-missing lookup did not stay null.' }
    $caseCount++
  }
  finally {
    if (Test-Path -LiteralPath $resolverRoot) {
      Remove-Item -LiteralPath $resolverRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  Write-Output 'Driver signability self-test passed (13 cases).'
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

$kitsRoot = 'C:\Program Files (x86)\Windows Kits\10'
$inf2cat = Resolve-Inf2Cat -WdkBin $env:WDK_BIN -KitsRoot $kitsRoot -GetCommandSource {
  (Get-Command Inf2Cat.exe -ErrorAction SilentlyContinue).Source
}
if ($null -eq $inf2cat) {
  throw 'Inf2Cat.exe not found; install the locked WDK or set WDK_BIN.'
}

& $inf2cat "/driver:$package" '/os:10_GE_X64,10_25H2_X64' '/verbose'
if ($LASTEXITCODE -ne 0) { throw "Inf2Cat failed with exit code $LASTEXITCODE." }
$catalog = Join-Path $package 'HibikiVirtualAudio.cat'
if (-not (Test-Path -LiteralPath $catalog)) { throw 'Inf2Cat did not produce HibikiVirtualAudio.cat.' }
Write-Output 'Driver signability check passed for Windows 11 24H2/25H2 x64.'
