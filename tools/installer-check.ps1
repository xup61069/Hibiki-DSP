[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function Get-InstallerFunctions([string]$text, [string]$sourceName) {
  $tokens = $null
  $errors = $null
  $ast = [System.Management.Automation.Language.Parser]::ParseInput($text, [ref]$tokens, [ref]$errors)
  if ($errors.Count -gt 0) { throw "Installer PowerShell parse errors in $sourceName`: $($errors -join '; ')" }
  $functions = @{}
  foreach ($function in $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
    if ($functions.ContainsKey($function.Name)) { throw "Duplicate installer function [$($function.Name)]: $sourceName" }
    $functions[$function.Name] = $function.Extent.Text
  }
  return ,$functions
}

function Assert-InstallerFunction($functions, [string]$name, [string[]]$patterns, [string]$sourceName) {
  if (-not $functions.ContainsKey($name)) { throw "Installer source is missing function [$name]: $sourceName" }
  $functionText = $functions[$name]
  foreach ($pattern in $patterns) {
    if ($functionText -notmatch $pattern) {
      throw "Installer function [$name] is missing boundary [$pattern]: $sourceName"
    }
  }
}

function Assert-InstallerSourcePolicy([string]$text, [string]$sourceName) {
  $functions = Get-InstallerFunctions $text $sourceName
  Assert-InstallerFunction $functions 'Get-Sha256' @(
    'Get-FileHash', '-Algorithm\s+SHA256'
  ) $sourceName
  Assert-InstallerFunction $functions 'Read-ReleaseManifest' @(
    'ConvertFrom-Json', 'schema_version', 'source_tag', 'source_commit', 'toolchain_digest',
    'dependency_lock_digest', 'sbom_digest', 'driver_package', 'microsoft_signature_thumbprint',
    'rfc3161_timestamp'
  ) $sourceName
  Assert-InstallerFunction $functions 'Test-ManifestFiles' @(
    'IsPathRooted', '\\\.\\\.', 'Get-Sha256', 'StartsWith', 'Test-Path'
  ) $sourceName
  Assert-InstallerFunction $functions 'Invoke-HibikiInstall' @(
    'ShouldProcess', 'pnputil\.exe', '/add-driver', '/install'
  ) $sourceName

  foreach ($pattern in @(
      '\[Parameter\(Mandatory\s*=\s*\$true\)\]\[string\]\$PackageRoot',
      '\[Parameter\(Mandatory\s*=\s*\$true\)\]\[string\]\$ManifestPath',
      '\[switch\]\$Apply',
      'WindowsPrincipal',
      'WindowsBuiltInRole',
      'Administrator'
  )) {
    if ($text -notmatch $pattern) { throw "Installer source is missing boundary [$pattern]: $sourceName" }
  }
}

if ($SelfTest) {
  $valid = @'
[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)][string]$PackageRoot,
  [Parameter(Mandatory = $true)][string]$ManifestPath,
  [switch]$Apply
)
function Get-Sha256([string]$Path) {
  (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Read-ReleaseManifest([string]$Path) {
  $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  if ($manifest.schema_version -ne 1) { throw 'Unsupported ReleaseManifest schema.' }
  $manifest.source_tag = $manifest.source_tag
  $manifest.source_commit = $manifest.source_commit
  $manifest.toolchain_digest = $manifest.toolchain_digest
  $manifest.dependency_lock_digest = $manifest.dependency_lock_digest
  $manifest.sbom_digest = $manifest.sbom_digest
  $manifest.driver_package.microsoft_signature_thumbprint = $manifest.driver_package.microsoft_signature_thumbprint
  $manifest.installer.rfc3161_timestamp = $manifest.installer.rfc3161_timestamp
  return $manifest
}
function Test-ManifestFiles($Manifest, [string]$Root) {
  foreach ($entry in @($Manifest.unsigned_files)) {
    if ([IO.Path]::IsPathRooted($entry.path) -or $entry.path -match '(^|[\\/])\.\.([\\/]|$)') { throw 'path' }
    $resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Root).Path).TrimEnd('\\') + '\\'
    if (-not ([IO.Path]::GetFullPath((Join-Path $Root $entry.path))).StartsWith($resolvedRoot)) { throw 'escape' }
    if (-not (Test-Path -LiteralPath (Join-Path $Root $entry.path))) { throw 'missing' }
    $actual = Get-Sha256 (Join-Path $Root $entry.path)
  }
}
function Invoke-HibikiInstall {
  [CmdletBinding(SupportsShouldProcess)]
  param([string]$Root, $Manifest)
  if ($PSCmdlet.ShouldProcess($Root, 'Stage signed Hibiki driver')) {
    & pnputil.exe /add-driver (Join-Path $Root 'HibikiVirtualAudio.inf') /install
  }
}
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator privileges required.' }
'@
  Assert-InstallerSourcePolicy $valid 'selftest-valid.ps1'

  $fixtures = @(
    @{ Name = 'missing-sha256'; Text = $valid.Replace('Get-FileHash', 'Get-OtherHash') },
    @{ Name = 'missing-path-confinement'; Text = $valid.Replace('[IO.Path]::IsPathRooted', '[IO.Path]::PathIsSafe') },
    @{ Name = 'missing-should-process'; Text = $valid.Replace('ShouldProcess', 'ShouldContinue') },
    @{ Name = 'missing-pnputil-install'; Text = $valid.Replace('pnputil.exe /add-driver', 'pnputil.exe /stage-driver') },
    @{ Name = 'missing-admin-gate'; Text = $valid.Replace('Administrator', 'NonAdmin') }
  )
  foreach ($fixture in $fixtures) {
    $caught = $false
    try { Assert-InstallerSourcePolicy $fixture.Text "selftest-$($fixture.Name).ps1" } catch { $caught = $true }
    if (-not $caught) { throw "Installer source self-test expected rejection: $($fixture.Name)" }
  }
  Write-Output "Installer source self-test passed ($($fixtures.Count + 1) cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'installer/HibikiSetup.ps1'
if (-not (Test-Path -LiteralPath $path)) { throw 'Missing installer/HibikiSetup.ps1.' }
Assert-InstallerSourcePolicy (Get-Content -LiteralPath $path -Raw) $path
Write-Output 'Installer source checks passed.'
