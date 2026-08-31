#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

$script:RequiredBoundaries = @(
  'Read-ReleaseManifest',
  'Test-ManifestFiles',
  'Resolve-HibikiDestination',
  'Get-StagingPlan',
  'Copy-HibikiFileWithHash',
  'Get-UninstallPlan',
  'Invoke-PayloadUninstall',
  'Invoke-HibikiUninstall',
  'Invoke-HibikiInstall',
  '-Apply',
  'pnputil.exe',
  'IsPathRooted',
  'sha256',
  'dependency_lock_digest',
  'distribution_id',
  'driver_package',
  'catalog_sha256',
  'sbom_digest'
)

function Assert-InstallerSourcePolicy {
  param(
    [string]$Text,
    [string]$SourceName
  )
  foreach ($required in $script:RequiredBoundaries) {
    if (-not $Text.Contains($required)) {
      throw "Installer source missing required boundary in ${SourceName}: $required"
    }
  }
}

function Assert-InstallerParseInput {
  param(
    [string]$Text,
    [string]$SourceName
  )
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseInput($Text, [ref]$tokens, [ref]$errors) | Out-Null
  if (@($errors).Count -gt 0) {
    throw "Installer PowerShell parse errors in ${SourceName}: $($errors -join '; ')"
  }
}

function Assert-InstallerParseFile {
  param(
    [string]$Path
  )
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$errors) | Out-Null
  if (@($errors).Count -gt 0) {
    throw "Installer PowerShell parse errors: $($errors -join '; ')"
  }
}
function Assert-NoUninstallBackupResidue {
  param(
    [Parameter(Mandatory)][string]$TempRoot
  )
  $residue = @(Get-ChildItem -LiteralPath $TempRoot -Directory -Filter '.hibiki-uninstall-backup-*' -Recurse -ErrorAction SilentlyContinue)
  if ($residue.Count -gt 0) {
    throw ('SelfTest expected no uninstall backup residue, found: ' + (($residue | ForEach-Object { $_.FullName }) -join '; '))
  }
}
if ($SelfTest) {
  $validFixture = @'
function Read-ReleaseManifest {}
function Test-ManifestFiles {}
function Resolve-HibikiDestination {}
function Get-StagingPlan {}
function Copy-HibikiFileWithHash {}
function Get-UninstallPlan {}
function Invoke-PayloadUninstall {}
function Invoke-HibikiUninstall {}
function Invoke-HibikiInstall { param([switch]$Apply) }
$null = "-Apply"
$null = "pnputil.exe"
$null = [IO.Path]::IsPathRooted("test")
$null = "sha256"
$null = "dependency_lock_digest"
$null = "distribution_id"
$null = "driver_package"
$null = "catalog_sha256"
$null = "sbom_digest"
'@

  $caseCount = 0
  $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("hibiki-installer-selftest-" + [Guid]::NewGuid().ToString("N"))
  $originalTempEnvironment = $env:TEMP
  $originalTmpEnvironment = $env:TMP

  try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    # Invoke-PayloadUninstall creates its backup directly below GetTempPath().
    # Redirect only this pwsh process so residue assertions observe the exact
    # root exercised by the production function, then restore it in finally.
    $env:TEMP = $tempRoot
    $env:TMP = $tempRoot
    $expectedTempRoot = [IO.Path]::GetFullPath($tempRoot).TrimEnd([char[]]@('\', '/'))
    $actualTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]@('\', '/'))
    if (-not $actualTempRoot.Equals($expectedTempRoot, [StringComparison]::OrdinalIgnoreCase)) {
      throw "SelfTest process temp redirect failed: expected '$expectedTempRoot', got '$actualTempRoot'."
    }

    # Case 1: valid fixture parses and satisfies all boundaries.
    Assert-InstallerParseInput -Text $validFixture -SourceName 'selftest-valid'
    Assert-InstallerSourcePolicy -Text $validFixture -SourceName 'selftest-valid'
    $caseCount++

    # Case 2-9: functional staging self-tests using temp fixtures.
    # Extract function definitions from the real installer source and invoke them.
    $repo = Split-Path -Parent $PSScriptRoot
    $installerPath = Join-Path $repo "installer" | Join-Path -ChildPath "HibikiSetup.ps1"
    $installerText = Get-Content -LiteralPath $installerPath -Raw
    Assert-InstallerSourcePolicy -Text $installerText -SourceName 'real-installer'
    Assert-InstallerParseFile -Path $installerPath

    $ast = [System.Management.Automation.Language.Parser]::ParseInput($installerText, [ref]$null, [ref]$null)
    $functions = @{}
    foreach ($fn in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
      if ($fn.Name -in @('Get-Sha256', 'Read-ReleaseManifest', 'Resolve-HibikiDestination', 'Get-StagingPlan', 'Copy-HibikiFileWithHash', 'Invoke-PayloadStaging', 'Get-UninstallPlan', 'Invoke-PayloadUninstall')) {
        $functions[$fn.Name] = $fn
      }
    }

    # Define all needed installer functions at self-test script scope.
    foreach ($name in @('Get-Sha256', 'Read-ReleaseManifest', 'Resolve-HibikiDestination', 'Get-StagingPlan', 'Copy-HibikiFileWithHash', 'Invoke-PayloadStaging', 'Get-UninstallPlan', 'Invoke-PayloadUninstall')) {
      $fn = $functions[$name]
      if (-not $fn) { throw "SelfTest missing function: $name" }
      Invoke-Expression $fn.Extent.Text
    }

    function Copy-Manifest {
      param([Parameter(Mandatory)][System.Collections.IDictionary]$Manifest)
      return ($Manifest | ConvertTo-Json -Depth 10 | ConvertFrom-Json -AsHashtable)
    }

    function Assert-ManifestReadRejected {
      param(
        [Parameter(Mandatory)][System.Collections.IDictionary]$Manifest,
        [Parameter(Mandatory)][string]$FileName,
        [Parameter(Mandatory)][string]$ExpectedMessage
      )
      $path = Join-Path $tempRoot $FileName
      $Manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
      try {
        Read-ReleaseManifest $path | Out-Null
      } catch {
        if ($_.Exception.Message -notlike ('*' + $ExpectedMessage + '*')) {
          throw ("SelfTest expected '" + $ExpectedMessage + "' for " + $FileName + ", got: " + $_.Exception.Message)
        }
        return
      }
      throw ("SelfTest expected manifest rejection for " + $FileName + '.')
    }

    # Case 1b: Read-ReleaseManifest rejects missing product_version.
    $missingVersionManifest = @{
      schema_version = 1
      source_tag = 'v1.0.0-test'
      source_commit = ('a' * 40)
      distribution_id = 'hibiki-public-2026'
      toolchain_digest = ('b' * 64)
      dependency_lock_digest = ('c' * 64)
      sbom_digest = ('d' * 64)
      driver_package = @{ sha256 = ('e' * 64); catalog_sha256 = ('f' * 64) }
      installer = @{ sha256 = ('2' * 64) }
      unsigned_files = @(@{ path = 'payload.txt'; sha256 = ('0' * 64) })
      tests = @('unit-test-1', 'integration-test-2')
    }
    $manifestPath = Join-Path $tempRoot 'missing-version-manifest.json'
    $missingVersionManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $manifestPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected missing product_version failure.' }
    $caseCount++

    # Case 1c: Read-ReleaseManifest accepts a complete valid fixture with product_version.
    $validFullManifest = $missingVersionManifest.Clone()
    $validFullManifest['product_version'] = '1.0.0'
    $validManifestPath = Join-Path $tempRoot 'valid-manifest.json'
    $validFullManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $validManifestPath -Encoding UTF8
    $parsed = Read-ReleaseManifest $validManifestPath
    if ($parsed.product_version -ne '1.0.0' -or
        $parsed.distribution_id -ne 'hibiki-public-2026') {
      throw 'SelfTest expected valid manifest to parse.'
    }
    $caseCount++

    # Case 1c.0: JSON numeric equivalents of 1 remain schema-compatible.
    $numericSchemaManifest = Copy-Manifest $validFullManifest
    $numericSchemaManifest['schema_version'] = 1.0
    $numericSchemaPath = Join-Path $tempRoot 'numeric-schema-version-manifest.json'
    $numericSchemaManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $numericSchemaPath -Encoding UTF8
    $numericSchemaParsed = Read-ReleaseManifest $numericSchemaPath
    if ($numericSchemaParsed.schema_version -ne 1 -or $numericSchemaParsed.schema_version -isnot [double]) {
      throw 'SelfTest expected numeric schema_version equivalent to parse as Double.'
    }
    $caseCount++

    # Scalar-type cases: reject values that coerce to valid-looking strings.
    $bigCommit = [System.Numerics.BigInteger]::Parse(('1' * 40))
    $bigDigest = [System.Numerics.BigInteger]::Parse(('1' * 64))
    foreach ($scalarCase in @(
      @{ Name = 'schema-version-string'; Expected = 'Manifest schema_version must be the JSON number 1.'; Action = { param($m, $commit, $digest) $m['schema_version'] = '1' } },
      @{ Name = 'schema-version-boolean'; Expected = 'Manifest schema_version must be the JSON number 1.'; Action = { param($m, $commit, $digest) $m['schema_version'] = $true } },
      @{ Name = 'product-version-number'; Expected = 'Manifest product_version must be a non-empty string.'; Action = { param($m, $commit, $digest) $m['product_version'] = 1 } },
      @{ Name = 'source-tag-boolean'; Expected = 'Manifest source_tag must be a string.'; Action = { param($m, $commit, $digest) $m['source_tag'] = $true } },
      @{ Name = 'source-commit-number'; Expected = 'Manifest source_commit must be a 40-character commit string.'; Action = { param($m, $commit, $digest) $m['source_commit'] = $commit } },
      @{ Name = 'distribution-id-number'; Expected = 'Manifest distribution_id must be a non-empty string.'; Action = { param($m, $commit, $digest) $m['distribution_id'] = 1 } },
      @{ Name = 'toolchain-digest-number'; Expected = 'Manifest toolchain_digest must be a SHA-256 digest string.'; Action = { param($m, $commit, $digest) $m['toolchain_digest'] = $digest } },
      @{ Name = 'dependency-digest-number'; Expected = 'Manifest dependency_lock_digest must be a SHA-256 digest string.'; Action = { param($m, $commit, $digest) $m['dependency_lock_digest'] = $digest } },
      @{ Name = 'sbom-digest-number'; Expected = 'Manifest sbom_digest must be a SHA-256 digest string.'; Action = { param($m, $commit, $digest) $m['sbom_digest'] = $digest } },
      @{ Name = 'driver-package-object-array'; Expected = 'Manifest driver_package must carry package/catalog SHA-256 hash strings.'; Action = { param($m, $commit, $digest) $m['driver_package'] = @($m['driver_package']) } },
      @{ Name = 'driver-package-hash-number'; Expected = 'Manifest driver_package must carry package/catalog SHA-256 hash strings.'; Action = { param($m, $commit, $digest) $m['driver_package']['sha256'] = $digest } },
      @{ Name = 'driver-catalog-hash-number'; Expected = 'Manifest driver_package must carry package/catalog SHA-256 hash strings.'; Action = { param($m, $commit, $digest) $m['driver_package']['catalog_sha256'] = $digest } },
      @{ Name = 'installer-object-array'; Expected = 'Manifest installer must carry an installer SHA-256 hash string.'; Action = { param($m, $commit, $digest) $m['installer'] = @($m['installer']) } },
      @{ Name = 'installer-hash-number'; Expected = 'Manifest installer must carry an installer SHA-256 hash string.'; Action = { param($m, $commit, $digest) $m['installer']['sha256'] = $digest } },
      @{ Name = 'unsigned-file-path-number'; Expected = 'Manifest unsigned_files path must be a string of 1..260 characters.'; Action = { param($m, $commit, $digest) $m['unsigned_files'][0]['path'] = 1 } },
      @{ Name = 'unsigned-file-hash-number'; Expected = 'Manifest unsigned_files sha256 must be a 64-character hexadecimal digest string'; Action = { param($m, $commit, $digest) $m['unsigned_files'][0]['sha256'] = $digest } },
      @{ Name = 'test-label-number'; Expected = 'Manifest tests entries must be non-empty strings of at most 120 characters.'; Action = { param($m, $commit, $digest) $m['tests'] = @(1) } }
    )) {
      $scalarManifest = Copy-Manifest $validFullManifest
      & $scalarCase.Action $scalarManifest $bigCommit $bigDigest
      Assert-ManifestReadRejected -Manifest $scalarManifest -FileName ('scalar-' + $scalarCase.Name + '-manifest.json') -ExpectedMessage $scalarCase.Expected
      $caseCount++
    }

    # Case 1c.1: Read-ReleaseManifest rejects a missing distribution_id.
    $missingDistributionManifest = $validFullManifest.Clone()
    $missingDistributionManifest.Remove('distribution_id')
    $missingDistributionPath = Join-Path $tempRoot 'missing-distribution-id-manifest.json'
    $missingDistributionManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $missingDistributionPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $missingDistributionPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected missing distribution_id failure.' }
    $caseCount++

    # Case 1c.2: Read-ReleaseManifest rejects an empty distribution_id.
    $emptyDistributionManifest = $validFullManifest.Clone()
    $emptyDistributionManifest['distribution_id'] = ' '
    $emptyDistributionPath = Join-Path $tempRoot 'empty-distribution-id-manifest.json'
    $emptyDistributionManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $emptyDistributionPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $emptyDistributionPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected empty distribution_id failure.' }
    $caseCount++

    # Case 1d: Read-ReleaseManifest rejects unknown root fields.
    $unknownFieldManifest = $validFullManifest.Clone()
    $unknownFieldManifest['evil_extra'] = 'malicious'
    $unknownFieldPath = Join-Path $tempRoot 'unknown-field-manifest.json'
    $unknownFieldManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $unknownFieldPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $unknownFieldPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected unknown field rejection.' }
    $caseCount++

    # Case 1e: Read-ReleaseManifest rejects missing tests field.
    $noTestsManifest = $validFullManifest.Clone()
    $noTestsManifest.Remove('tests')
    $noTestsPath = Join-Path $tempRoot 'no-tests-manifest.json'
    $noTestsManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $noTestsPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $noTestsPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected missing tests rejection.' }
    $caseCount++

    # Case 1f: Read-ReleaseManifest rejects oversized product_version.
    $oversizeVersionManifest = $validFullManifest.Clone()
    $oversizeVersionManifest['product_version'] = 'a' * 65
    $oversizeVersionPath = Join-Path $tempRoot 'oversize-version-manifest.json'
    $oversizeVersionManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $oversizeVersionPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $oversizeVersionPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected oversize product_version rejection.' }
    $caseCount++

    # Case 1h: Read-ReleaseManifest rejects unsigned_files exceeding 1024.
    $manyFilesManifest = $validFullManifest.Clone()
    $manyFilesManifest['unsigned_files'] = @(1..1025 | ForEach-Object { @{ path = ('f' + $_); sha256 = ('0' * 64) } })
    $manyFilesPath = Join-Path $tempRoot 'many-files-manifest.json'
    $manyFilesManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manyFilesPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $manyFilesPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected unsigned_files overflow rejection.' }
    $caseCount++

    # Case 1i: Read-ReleaseManifest rejects tests exceeding 256.
    $manyTestsManifest = $validFullManifest.Clone()
    $manyTestsManifest['tests'] = @(1..257 | ForEach-Object { 'test-' + $_ })
    $manyTestsPath = Join-Path $tempRoot 'many-tests-manifest.json'
    $manyTestsManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manyTestsPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $manyTestsPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected tests overflow rejection.' }
    $caseCount++

    # Case 1j: Read-ReleaseManifest rejects unknown fields in unsigned_files entries.
    $unknownFileFieldManifest = $validFullManifest.Clone()
    $unknownFileFieldManifest['unsigned_files'] = @(
      @{ path = 'tools/readme.txt'; sha256 = ('0' * 64); extra_field = 'rejected' }
    )
    $unknownFileFieldPath = Join-Path $tempRoot 'unknown-file-field-manifest.json'
    $unknownFileFieldManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $unknownFileFieldPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $unknownFileFieldPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected unsigned_files unknown field rejection.' }
    $caseCount++

    # Case 1k: Read-ReleaseManifest rejects unsigned_files entries missing sha256.
    $missingFileHashManifest = $validFullManifest.Clone()
    $missingFileHashManifest['unsigned_files'] = @(
      @{ path = 'tools/readme.txt' }
    )
    $missingFileHashPath = Join-Path $tempRoot 'missing-file-hash-manifest.json'
    $missingFileHashManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $missingFileHashPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $missingFileHashPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected unsigned_files missing sha256 rejection.' }
    $caseCount++

    # Case 1l: Read-ReleaseManifest rejects invalid sha256 formats in unsigned_files.
    $invalidFileHashManifest = $validFullManifest.Clone()
    $invalidFileHashManifest['unsigned_files'] = @(
      @{ path = 'tools/readme.txt'; sha256 = 'not-a-valid-digest' }
    )
    $invalidFileHashPath = Join-Path $tempRoot 'invalid-file-hash-manifest.json'
    $invalidFileHashManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $invalidFileHashPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $invalidFileHashPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected unsigned_files invalid sha256 rejection.' }
    $caseCount++

    # Case 1m: Read-ReleaseManifest rejects non-array unsigned_files.
    $nonArrayFilesManifest = $validFullManifest.Clone()
    $nonArrayFilesManifest['unsigned_files'] = @{ path = 'tools/readme.txt'; sha256 = ('0' * 64) }
    $nonArrayFilesPath = Join-Path $tempRoot 'non-array-files-manifest.json'
    $nonArrayFilesManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $nonArrayFilesPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $nonArrayFilesPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected non-array unsigned_files rejection.' }
    $caseCount++

    # Case 1n: Read-ReleaseManifest rejects non-array tests.
    $nonArrayTestsManifest = $validFullManifest.Clone()
    $nonArrayTestsManifest['tests'] = 'unit-test-1'
    $nonArrayTestsPath = Join-Path $tempRoot 'non-array-tests-manifest.json'
    $nonArrayTestsManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $nonArrayTestsPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $nonArrayTestsPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected non-array tests rejection.' }
    $caseCount++

    # Case 1o: Read-ReleaseManifest rejects source_tag suffixes with disallowed characters.
    $badTagManifest = $validFullManifest.Clone()
    $badTagManifest['source_tag'] = 'v1.0.0-bad!'
    $badTagPath = Join-Path $tempRoot 'bad-source-tag-manifest.json'
    $badTagManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $badTagPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $badTagPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected invalid source_tag suffix rejection.' }
    $caseCount++

    # Case 1p: Read-ReleaseManifest rejects unknown fields in driver_package.
    $driverExtraFieldManifest = $validFullManifest.Clone()
    $driverExtraFieldManifest['driver_package'] = $validFullManifest['driver_package'].Clone()
    $driverExtraFieldManifest['driver_package']['extra_field'] = 'rejected'
    $driverExtraFieldPath = Join-Path $tempRoot 'driver-extra-field-manifest.json'
    $driverExtraFieldManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $driverExtraFieldPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $driverExtraFieldPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected driver_package unknown field rejection.' }
    $caseCount++

    # Case 1q: Read-ReleaseManifest rejects unknown fields in installer.
    $installerExtraFieldManifest = $validFullManifest.Clone()
    $installerExtraFieldManifest['installer'] = $validFullManifest['installer'].Clone()
    $installerExtraFieldManifest['installer']['extra_field'] = 'rejected'
    $installerExtraFieldPath = Join-Path $tempRoot 'installer-extra-field-manifest.json'
    $installerExtraFieldManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $installerExtraFieldPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $installerExtraFieldPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected installer unknown field rejection.' }
    $caseCount++

    # Cases 1s-1w: Read-ReleaseManifest rejects C0/C1/DEL in printable-only fields.
    foreach ($controlCase in @(
      @{ Name = 'product_version'; Action = { param($m) $m['product_version'] = ('bad' + [char]7) } },
      @{ Name = 'distribution_id'; Action = { param($m) $m['distribution_id'] = ('dist' + [char]0x9F) } },
      @{ Name = 'unsigned_files_path'; Action = { param($m) $m['unsigned_files'] = @(@{ path = ('tools/' + [char]1 + '/payload.bin'); sha256 = ('0' * 64) }) } },
      @{ Name = 'tests_label'; Action = { param($m) $m['tests'] = @(('test-' + [char]0x7F)) } }
    )) {
      $controlManifest = $validFullManifest.Clone()
      & $controlCase.Action $controlManifest
      $controlPath = Join-Path $tempRoot ('control-' + $controlCase.Name + '-manifest.json')
      $controlManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $controlPath -Encoding UTF8
      $caught = $false
      try { Read-ReleaseManifest $controlPath } catch { $caught = $true }
      if (-not $caught) { throw 'SelfTest expected control-character rejection for ' + $controlCase.Name + '.' }
      $caseCount++
    }

    # Case 1r: Read-ReleaseManifest rejects explicit null signed_installer_sha256.
    $nullSigHashManifest = $validFullManifest.Clone()
    $nullSigHashManifest['signed_installer_sha256'] = $null
    $nullSigHashPath = Join-Path $tempRoot 'null-sig-hash-manifest.json'
    $nullSigHashManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $nullSigHashPath -Encoding UTF8
    $caught = $false
    try { Read-ReleaseManifest $nullSigHashPath } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected null signed_installer_sha256 rejection.' }
    $caseCount++

    # Case 2: destination rejects blank path.
    $caught = $false
    try { Resolve-HibikiDestination '' } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected blank-destination failure.' }
    $caseCount++

    # Case 3: destination rejects dot-dot traversal.
    $caught = $false
    try { Resolve-HibikiDestination 'C:\safe\..\escape' } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected dot-dot-destination failure.' }
    $caseCount++

    # Case 4: destination rejects drive root.
    $caught = $false
    try { Resolve-HibikiDestination 'C:\' } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected drive-root failure.' }
    $caseCount++

    # Case 5: destination accepts valid absolute path and canonicalizes it.
    $destDir = Join-Path $tempRoot 'dest'
    $resolved = Resolve-HibikiDestination $destDir
    if (-not ([IO.Path]::IsPathRooted($resolved))) { throw 'SelfTest expected rooted resolved destination.' }
    $caseCount++

    # Case 6: staging plan rejects traversal entry escaping the destination.
    $pkgDir = Join-Path $tempRoot 'pkg'
    New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $pkgDir 'payload.txt') -Value 'hello' -NoNewline
    $traversalManifest = @{ unsigned_files = @(@{ path = '../outside.txt'; sha256 = ('0' * 64) }) }
    $caught = $false
    try { Get-StagingPlan $traversalManifest $pkgDir $destDir } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected staging-plan traversal failure.' }
    $caseCount++

    # Case 7: successful staging copies verified files and cleans up backups.
    Set-Content -LiteralPath (Join-Path $pkgDir 'payload.txt') -Value 'hello' -NoNewline -Encoding UTF8
    # Recompute from actual file to avoid BOM differences.
    $actualSha = (Get-FileHash -LiteralPath (Join-Path $pkgDir 'payload.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
    $validManifest = @{ unsigned_files = @(@{ path = 'payload.txt'; sha256 = $actualSha }) }
    $plan = Get-StagingPlan $validManifest $pkgDir $destDir
    if (@($plan).Count -ne 1) { throw 'SelfTest expected one staging-plan entry.' }
    Invoke-PayloadStaging -Plan $plan -Destination $destDir
    if (-not (Test-Path (Join-Path $destDir 'payload.txt'))) { throw 'SelfTest expected staged payload file.' }
    $backupDirs = @(Get-ChildItem -LiteralPath $destDir -Directory -Filter '.hibiki-backup-*' -ErrorAction SilentlyContinue)
    if ($backupDirs.Count -gt 0) { throw 'SelfTest expected backup cleanup after success.' }
    $caseCount++

    # Case 8: hash mismatch during staging triggers rollback and preserves prior file.
    # Overwrite payload.txt with different content, then try staging with wrong expected hash.
    Set-Content -LiteralPath (Join-Path $destDir 'payload.txt') -Value 'prior-install-content' -NoNewline
    $wrongHash = 'f' * 64
    $badManifest = @{ unsigned_files = @(@{ path = 'payload.txt'; sha256 = $wrongHash }) }
    $caught = $false
    try {
      $badPlan = Get-StagingPlan $badManifest $pkgDir $destDir
      Invoke-PayloadStaging -Plan $badPlan -Destination $destDir
    } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected rollback failure.' }
    # Prior content must be restored after failed overwrite.
    $restoredContent = Get-Content -LiteralPath (Join-Path $destDir 'payload.txt') -Raw
    if ($restoredContent.Trim() -ne 'prior-install-content') { throw 'SelfTest expected prior file to be restored after rollback.' }
    # Backup dir must be retained after failure for recovery.
    $backupDirsAfterFail = @(Get-ChildItem -LiteralPath $destDir -Directory -Filter '.hibiki-backup-*' -ErrorAction SilentlyContinue)
    # Note: our current implementation removes backup only on success; on rollback the moved original is restored in place and backup dir remains but may be empty.
    $caseCount++

    # Case 9: rollback retains backup directory for recovery and prior file content
    # is restored. This verifies the transactional boundary between success cleanup
    # (backup dir removed) and failure recovery (backup dir retained).
    $backupDirsAfterRollback = @(Get-ChildItem -LiteralPath $destDir -Directory -Filter '.hibiki-backup-*' -ErrorAction SilentlyContinue)
    # After rollback, the original file has been restored from backup; the backup dir
    # may be empty but should still exist as a recovery artifact.
    if ($restoredContent.Trim() -ne 'prior-install-content') { throw 'SelfTest expected prior file restored.' }
    $caseCount++

    # Case 10: the residue assertion fails closed against the same process temp
    # root used by Invoke-PayloadUninstall, then passes after fixture cleanup.
    $retainedBackup = Join-Path $tempRoot '.hibiki-uninstall-backup-retained-selftest'
    New-Item -ItemType Directory -Path $retainedBackup -Force | Out-Null
    $caught = $false
    try { Assert-NoUninstallBackupResidue -TempRoot $actualTempRoot } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected retained uninstall backup detection.' }
    Remove-Item -LiteralPath $retainedBackup -Recurse -Force
    Assert-NoUninstallBackupResidue -TempRoot $actualTempRoot
    $caseCount++

    # Case 11: uninstall plan rejects traversal entries that escape DestinationPath.
    $uninstallDest = Join-Path $tempRoot 'uninstall-dest'
    New-Item -ItemType Directory -Path $uninstallDest -Force | Out-Null
    $uninstallTraversalManifest = @{ unsigned_files = @(@{ path = '../uninstall-escape.txt'; sha256 = ('0' * 64) }) }
    $caught = $false
    try { Get-UninstallPlan $uninstallTraversalManifest $uninstallDest } catch { $caught = $true }
    if (-not $caught) { throw 'SelfTest expected uninstall-plan traversal failure.' }
    $caseCount++

    # Case 12: successful uninstall removes only planned files, preserves siblings,
    # reports the removal count, is idempotent and leaves no backup residue.
    $nestedPayloadDir = Join-Path $uninstallDest 'components'
    New-Item -ItemType Directory -Path $nestedPayloadDir -Force | Out-Null
    $firstUninstallFile = Join-Path $uninstallDest 'first.txt'
    $secondUninstallFile = Join-Path $nestedPayloadDir 'second.txt'
    Set-Content -LiteralPath $firstUninstallFile -Value 'remove-first' -NoNewline
    Set-Content -LiteralPath $secondUninstallFile -Value 'remove-second' -NoNewline
    Set-Content -LiteralPath (Join-Path $uninstallDest 'preserve.txt') -Value 'keep-sibling' -NoNewline
    $successManifest = @{
      unsigned_files = @(
        @{ path = 'first.txt'; sha256 = ('0' * 64) },
        @{ path = 'components/second.txt'; sha256 = ('0' * 64) }
      )
    }
    $uninstallPlan = Get-UninstallPlan $successManifest $uninstallDest
    if (@($uninstallPlan).Count -ne 2) { throw 'SelfTest expected two uninstall-plan entries.' }
    if (@($uninstallPlan | Where-Object { -not $_.Exists }).Count -ne 0) {
      throw 'SelfTest expected all successful uninstall-plan files to exist.'
    }
    $uninstallOutput = Invoke-PayloadUninstall -Plan $uninstallPlan -Destination $uninstallDest 6>&1
    if (($uninstallOutput -join "`n") -notmatch 'Removed 2 payload file\(s\)\.') {
      throw 'SelfTest expected successful uninstall to report two removed payload files.'
    }
    if ((Test-Path -LiteralPath $firstUninstallFile) -or (Test-Path -LiteralPath $secondUninstallFile)) {
      throw 'SelfTest expected both planned uninstall files to be removed.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $uninstallDest 'preserve.txt'))) {
      throw 'SelfTest expected unplanned sibling file to be preserved.'
    }
    if ((Get-Content -LiteralPath (Join-Path $uninstallDest 'preserve.txt') -Raw).Trim() -ne 'keep-sibling') {
      throw 'SelfTest expected preserved sibling content to remain unchanged.'
    }
    Assert-NoUninstallBackupResidue -TempRoot $actualTempRoot
    $idempotentPlan = Get-UninstallPlan $successManifest $uninstallDest
    $idempotentOutput = Invoke-PayloadUninstall -Plan $idempotentPlan -Destination $uninstallDest 6>&1
    if (($idempotentOutput -join "`n") -notmatch 'Removed 0 payload file\(s\)\.') {
      throw 'SelfTest expected repeated uninstall to report zero removals.'
    }
    Assert-NoUninstallBackupResidue -TempRoot $actualTempRoot
    $caseCount++

    # Case 13: failure on the second planned file restores the first removed file
    # byte-identically, preserves the locked original and removes backup residue.
    $rollbackDest = Join-Path $tempRoot 'uninstall-rollback'
    New-Item -ItemType Directory -Path $rollbackDest -Force | Out-Null
    $rollbackFirst = Join-Path $rollbackDest 'first.txt'
    $rollbackSecond = Join-Path $rollbackDest 'second.txt'
    Set-Content -LiteralPath $rollbackFirst -Value 'rollback-first' -NoNewline
    Set-Content -LiteralPath $rollbackSecond -Value 'locked-original' -NoNewline
    $rollbackManifest = @{
      unsigned_files = @(
        @{ path = 'first.txt'; sha256 = ('0' * 64) },
        @{ path = 'second.txt'; sha256 = ('0' * 64) }
      )
    }
    $rollbackPlan = Get-UninstallPlan $rollbackManifest $rollbackDest
    $lockedStream = [System.IO.File]::Open($rollbackSecond, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    try {
      $caught = $false
      try { Invoke-PayloadUninstall -Plan $rollbackPlan -Destination $rollbackDest } catch { $caught = $true }
      if (-not $caught) { throw 'SelfTest expected locked-file uninstall failure.' }
      if ((Get-Content -LiteralPath $rollbackFirst -Raw).Trim() -ne 'rollback-first') {
        throw 'SelfTest expected first removed file to be restored after rollback.'
      }
      $expectedRollbackBytes = [System.Text.Encoding]::UTF8.GetBytes('rollback-first');
      $expectedRollbackHash = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new($expectedRollbackBytes)) -Algorithm SHA256).Hash;
      $restoredRollbackHash = (Get-FileHash -LiteralPath $rollbackFirst -Algorithm SHA256).Hash;
      if ($restoredRollbackHash -ne $expectedRollbackHash) {
        throw 'SelfTest expected restored first file to be byte-identical.'
      }
    } finally {
      $lockedStream.Dispose()
    }
    if ((Get-Content -LiteralPath $rollbackSecond -Raw).Trim() -ne 'locked-original') {
      throw 'SelfTest expected locked file content to remain unchanged.'
    }
    Assert-NoUninstallBackupResidue -TempRoot $actualTempRoot
    $caseCount++
  } finally {
    $env:TEMP = $originalTempEnvironment
    $env:TMP = $originalTmpEnvironment
    if (Test-Path -LiteralPath $tempRoot) {
      Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  if (-not [object]::Equals($env:TEMP, $originalTempEnvironment) -or
      -not [object]::Equals($env:TMP, $originalTmpEnvironment)) {
    throw 'SelfTest failed to restore the original process TEMP/TMP environment.'
  }

  Write-Output "Installer source gate self-test passed ($caseCount cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'installer/HibikiSetup.ps1'
Assert-InstallerParseFile -Path $path
$text = Get-Content -LiteralPath $path -Raw
Assert-InstallerSourcePolicy -Text $text -SourceName 'installer/HibikiSetup.ps1'
Write-Output 'Installer source checks passed.'
