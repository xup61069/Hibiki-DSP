[CmdletBinding()]
param(
  [Parameter(Mandatory = $false)]
  [string]$PackageRoot,
  [switch]$RequireInf2Cat
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$inf = Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf'
if (-not (Test-Path -LiteralPath $inf)) { throw 'Missing source-only HibikiVirtualAudio.inf.' }
$infText = Get-Content -LiteralPath $inf -Raw
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
