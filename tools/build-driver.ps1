[CmdletBinding()]
param(
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = (Get-Location).Path
$kits = 'C:\Program Files (x86)\Windows Kits\10'
$kver = '10.0.28000.0'
$incRoot = Join-Path $kits "Include\$kver"
$libRoot = Join-Path $kits "Lib\$kver"
$inf2cat = Join-Path $kits "bin\$kver\x86\Inf2Cat.exe"

function Assert-Tool {
  param([string]$Path, [string]$Label)
  if (-not (Test-Path -LiteralPath $Path)) { throw "$Label not found: $Path" }
}

# --- Toolchain discovery -------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
Assert-Tool $vswhere 'vswhere.exe'
$cl = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
  -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\cl.exe' 2>$null | Select-Object -First 1
if ($cl) { $linkDir = Split-Path -Parent $cl }
else { throw 'cl.exe could not be located via vswhere.' }
$link = Join-Path $linkDir 'link.exe'
Assert-Tool $cl 'cl.exe'
Assert-Tool $link 'link.exe'
Assert-Tool $inf2cat 'Inf2Cat.exe'
foreach ($d in @('km', 'shared')) {
  Assert-Tool (Join-Path $incRoot $d) "WDK Include\$d"
}
$kmLib = Join-Path $libRoot 'km\x64'
Assert-Tool $kmLib 'WDK km x64 libs'

Write-Output "Toolchain: cl=$cl"
Write-Output "Toolchain: WDK=$incRoot"

# --- Layout ---------------------------------------------------------------
$objDir = Join-Path $repo '.local/driver-build/obj'
$pkgDir = Join-Path $repo '.local/driver-package'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null

# --- Compile kernel-mode x64 ----------------------------------------------
$clDir = Split-Path -Parent $cl
$msvcInc = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $clDir))) 'include'
$env:INCLUDE = "$incRoot\km;$incRoot\km\crt;$incRoot\shared;$msvcInc;$repo\driver\include;$repo\sdk\include"
$defines = @(
  '/D_NTDDK_', '/D_AMD64_', '/DAMD64', '/D_WIN64', '/DWINNT=1',
  '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', '/DNTDDI_VERSION=0x0A000000',
  '/DPOOL_NX_OPTIN=1'
)
# The WDK headers also define _NTDDK_; the duplicate-definition notice is
# expected when the guard is provided by the build environment instead.
$flags = @('/nologo', '/W4', '/wd4005', '/kernel', '/c', '/Zp8', '/GR-', '/GS', '/EHs-c-')
$objs = @()
foreach ($cpp in (Get-ChildItem (Join-Path $repo 'driver/wdk') -Filter *.cpp)) {
  $obj = Join-Path $objDir ($cpp.BaseName + '.obj')
  & $cl @flags @defines "/Fo$obj" $cpp.FullName
  if ($LASTEXITCODE -ne 0) { throw "cl.exe failed for $($cpp.Name)." }
  $objs += $obj
  Write-Output "compiled $($cpp.Name)"
}

# --- Link .sys ------------------------------------------------------------
$sysPath = Join-Path $pkgDir 'HibikiVirtualAudio.sys'
$env:LIB = "$kmLib"
& $link /nologo /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry `
  "/OUT:$sysPath" `
  $objs ntoskrnl.lib hal.lib ks.lib portcls.lib stdunk.lib libcntpr.lib bufferoverflowK.lib
if ($LASTEXITCODE -ne 0) { throw 'link.exe failed producing HibikiVirtualAudio.sys.' }
if (-not (Test-Path $sysPath)) { throw 'HibikiVirtualAudio.sys was not produced.' }
Write-Output "linked $sysPath"

# --- Package + Inf2Cat ----------------------------------------------------
Copy-Item (Join-Path $repo 'driver/inf/HibikiVirtualAudio.inf') $pkgDir -Force
& $inf2cat /driver:$pkgDir /os:10_X64
if ($LASTEXITCODE -ne 0) { throw 'Inf2Cat failed producing HibikiVirtualAudio.cat.' }
$cat = Join-Path $pkgDir 'HibikiVirtualAudio.cat'
if (-not (Test-Path $cat)) { throw 'HibikiVirtualAudio.cat was not produced.' }

Write-Output 'Driver package complete:'
Get-ChildItem $pkgDir | ForEach-Object { Write-Output ("  " + $_.Name) }
