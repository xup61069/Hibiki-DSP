[CmdletBinding()]
param(
  [string]$Configuration = 'Release',
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-DriverBuildPaths {
  param([Parameter(Mandatory = $true)][string]$RepoRoot)

  $resolvedRepo = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
  [ordered]@{
    RepoRoot = $resolvedRepo
    ObjectRoot = Join-Path $resolvedRepo '.local/driver-build/obj'
    PackageRoot = Join-Path $resolvedRepo '.local/driver-package'
  }
}

function Test-PathUnderRoot {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Candidate
  )

  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate)
  $resolvedCandidate.StartsWith(
    $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase
  )
}

$paths = Get-DriverBuildPaths -RepoRoot $repo
if ($SelfTest) {
  $expectedRepo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
  if ($paths.RepoRoot -ne $expectedRepo) {
    throw "build-driver self-test resolved the wrong repository root: $($paths.RepoRoot)"
  }

  $localRoot = Join-Path $paths.RepoRoot '.local'
  foreach ($outputPath in @($paths.ObjectRoot, $paths.PackageRoot)) {
    if (-not (Test-PathUnderRoot -Root $localRoot -Candidate $outputPath)) {
      throw "build-driver self-test found an output path outside .local: $outputPath"
    }
  }

  foreach ($requiredPath in @(
      (Join-Path $paths.RepoRoot 'driver/src'),
      (Join-Path $paths.RepoRoot 'driver/wdk'),
      (Join-Path $paths.RepoRoot 'sdk/include')
    )) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
      throw "build-driver self-test could not find repository source boundary: $requiredPath"
    }
  }

  Write-Output 'Driver build path self-test passed (script-root discovery, .local outputs, source boundaries).'
  exit 0
}

$kits = 'C:\Program Files (x86)\Windows Kits\10'
$kver = Get-ChildItem (Join-Path $kits 'Include') -Directory |
  Where-Object { Test-Path (Join-Path $_.FullName 'km') } |
  Sort-Object Name -Descending |
  Select-Object -First 1 -ExpandProperty Name
if (-not $kver) { throw 'No Windows Kits include directory with km headers found.' }
$incRoot = Join-Path $kits "Include\$kver"
$libRoot = Join-Path $kits "Lib\$kver"
# Some WDK releases place Inf2Cat outside the matching version folder;
# discover it under the kits bin tree instead of assuming a fixed path.
$inf2cat = Get-ChildItem (Join-Path $kits 'bin') -Recurse -Filter Inf2Cat.exe -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending |
  Select-Object -First 1 -ExpandProperty FullName

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
$objDir = $paths.ObjectRoot
$pkgDir = $paths.PackageRoot
New-Item -ItemType Directory -Path $objDir -Force | Out-Null
New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null

# --- Compile kernel-mode x64 ----------------------------------------------
$clDir = Split-Path -Parent $cl
$msvcInc = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $clDir))) 'include'
$env:INCLUDE = "$incRoot\km;$incRoot\km\crt;$incRoot\shared;$msvcInc;$repo\driver\include;$repo\sdk\include"
$defines = @(
  '/D_AMD64_', '/DAMD64', '/D_WIN64', '/DWINNT=1',
  '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', '/DNTDDI_VERSION=0x0A000000',
  '/DPOOL_NX_OPTIN=1'
)
# The WDK headers also define _NTDDK_; the duplicate-definition notice is
# expected when the guard is provided by the build environment instead.
$flags = @('/nologo', "/FI$incRoot\km\ntddk.h", '/W4', '/wd4005', '/kernel', '/c', '/Zp8', '/GR-', '/GS', '/EHs-c-', '/Zl')
$objs = @()
foreach ($cfile in (Get-ChildItem (Join-Path $repo 'driver/src') -Filter *.c)) {
  $obj = Join-Path $objDir ($cfile.BaseName + '.obj')
  & $cl @flags @defines "/Fo$obj" $cfile.FullName
  if ($LASTEXITCODE -ne 0) { throw "cl.exe failed for $($cfile.Name)." }
  $objs += $obj
}
$guidsCpp = Join-Path $objDir 'guids.cpp'
Copy-Item (Join-Path $repo 'driver/wdk/guids.cpp') $guidsCpp -Force
$obj = Join-Path $objDir 'guids.obj'
& $cl @flags @defines "/Fo$obj" $guidsCpp
if ($LASTEXITCODE -ne 0) { throw 'cl.exe failed for guids.cpp.' }
$objs += $obj
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
$env:WDK_BIN = Split-Path -Parent $inf2cat
& $link /nologo /DRIVER /SUBSYSTEM:NATIVE,10.00 /ENTRY:GsDriverEntry /NODEFAULTLIB /INTEGRITYCHECK `
  "/OUT:$sysPath" `
  $objs ntoskrnl.lib hal.lib ks.lib portcls.lib stdunk.lib libcntpr.lib ksguid.lib bufferoverflowK.lib
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
