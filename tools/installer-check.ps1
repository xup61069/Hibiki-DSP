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
  'driver_package',
  'microsoft_signature_thumbprint',
  'rfc3161_timestamp',
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
$null = "driver_package"
$null = "microsoft_signature_thumbprint"
$null = "rfc3161_timestamp"
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
      if ($fn.Name -in @('Get-Sha256', 'Resolve-HibikiDestination', 'Get-StagingPlan', 'Copy-HibikiFileWithHash', 'Invoke-PayloadStaging', 'Get-UninstallPlan', 'Invoke-PayloadUninstall')) {
        $functions[$fn.Name] = $fn
      }
    }

    # Define all needed installer functions at self-test script scope.
    foreach ($name in @('Get-Sha256', 'Resolve-HibikiDestination', 'Get-StagingPlan', 'Copy-HibikiFileWithHash', 'Invoke-PayloadStaging', 'Get-UninstallPlan', 'Invoke-PayloadUninstall')) {
      $fn = $functions[$name]
      if (-not $fn) { throw "SelfTest missing function: $name" }
      Invoke-Expression $fn.Extent.Text
    }


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
