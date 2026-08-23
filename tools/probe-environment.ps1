#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$local = Join-Path $repo '.local'

$ExpectedProbeFormats = @('LPCM 2.0', 'LPCM 5.1', 'LPCM 7.1')
$ForbiddenProbeKeys = @(
  'endpoint',
  'endpoint_id',
  'device_id',
  'friendly_name',
  'identity',
  'pid',
  'serial',
  'session_id',
  'private_path',
  'calibration_path',
  'user_profile'
)

function Test-EnvironmentProbeProperty {
  param(
    [AllowNull()]$Object,
    [Parameter(Mandatory = $true)][string]$Name
  )

  if ($null -eq $Object) {
    return $false
  }

  if ($Object -is [System.Collections.IDictionary]) {
    return $Object.Contains($Name)
  }

  return $null -ne $Object.PSObject.Properties[$Name]
}

function Get-EnvironmentProbeProperty {
  param(
    [AllowNull()]$Object,
    [Parameter(Mandatory = $true)][string]$Name
  )

  if (-not (Test-EnvironmentProbeProperty -Object $Object -Name $Name)) {
    return $null
  }

  if ($Object -is [System.Collections.IDictionary]) {
    return $Object[$Name]
  }

  return $Object.PSObject.Properties[$Name].Value
}

function Get-EnvironmentProbePropertyName {
  param(
    [AllowNull()]$Value
  )

  if ($null -eq $Value) {
    return
  }

  if ($Value -is [System.Collections.IDictionary]) {
    foreach ($key in $Value.Keys) {
      [string]$key
      Get-EnvironmentProbePropertyName -Value $Value[$key]
    }

    return
  }

  if ($Value -is [pscustomobject]) {
    foreach ($property in $Value.PSObject.Properties) {
      [string]$property.Name
      Get-EnvironmentProbePropertyName -Value $property.Value
    }

    return
  }

  if (($Value -is [System.Collections.IEnumerable]) -and ($Value -isnot [string])) {
    foreach ($item in $Value) {
      Get-EnvironmentProbePropertyName -Value $item
    }
  }
}

function New-EnvironmentProbeDocument {
  param(
    [AllowNull()][string]$OsCaption,
    [int]$OsBuild,
    [AllowNull()][string]$Architecture,
    [AllowNull()][string]$CmakeVersion,
    [AllowNull()][string]$GitVersion,
    [AllowNull()][string[]]$SupportedFormats,
    [Parameter(Mandatory = $true)][string]$CapturedAtUtc
  )

  return [ordered]@{
    schema_version = 1
    captured_at_utc = $CapturedAtUtc
    os = [ordered]@{
      caption = $OsCaption
      build = $OsBuild
      architecture = $Architecture
    }
    tools = [ordered]@{
      cmake = $CmakeVersion
      git = $GitVersion
    }
    audio = [ordered]@{
      note = 'Capture actual endpoint IDs and private calibration only in local user storage; never commit them.'
      supported_formats = $SupportedFormats
    }
  }
}

function Copy-EnvironmentProbeValue {
  param(
    [AllowNull()]$Value
  )

  if ($null -eq $Value) {
    return $null
  }

  if ($Value -is [System.Collections.IDictionary]) {
    $copy = [ordered]@{}
    foreach ($entry in $Value.GetEnumerator()) {
      $copy[$entry.Key] = Copy-EnvironmentProbeValue -Value $entry.Value
    }

    return $copy
  }

  if (($Value -is [System.Collections.IEnumerable]) -and ($Value -isnot [string])) {
    $copy = @()
    foreach ($item in $Value) {
      $copy += Copy-EnvironmentProbeValue -Value $item
    }

    return $copy
  }

  return $Value
}

function Copy-EnvironmentProbeDocument {
  param(
    [Parameter(Mandatory = $true)]$Document
  )

  return Copy-EnvironmentProbeValue -Value $Document
}

function Test-EnvironmentProbePathUnderRoot {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath -eq $fullRoot -or $fullPath.StartsWith(
    $fullRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase
  )
}

function Get-EnvironmentProbeExistingAttributes {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Environment probe path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Environment probe path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-EnvironmentProbePath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-EnvironmentProbePathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Environment probe path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-EnvironmentProbeExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Environment probe $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-EnvironmentProbeExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Environment probe path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Environment probe path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Environment probe path is not a file: $fullPath"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Environment probe path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [System.IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Environment probe path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-EnvironmentProbePathSelfTest {
  $repositoryRoot = [System.IO.Path]::GetFullPath('C:\hibiki-environment-probe-selftest').TrimEnd('\', '/')
  $localRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot '.local')).TrimEnd('\', '/')
  $contextPath = [System.IO.Path]::GetFullPath((Join-Path $localRoot 'context.json')).TrimEnd('\', '/')
  $outsidePath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'outside-context')).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-EnvironmentProbePath -Path $localRoot -Root $repositoryRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -SyntheticAttributes @{
    $repositoryRoot = $directory
    $localRoot = $directory
    $contextPath = $file
  }
  $cases++

  $outsideCaught = $false
  try { Assert-EnvironmentProbePath -Path $outsidePath -Root $localRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Environment probe self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{
      $repositoryRoot = $directory
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Environment probe self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseLeafCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{
      $repositoryRoot = $directory
      $localRoot = $directory
      $contextPath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseLeafCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseLeafCaught) { throw 'Environment probe self-test expected a reparse-leaf rejection.' }
  $cases++

  $wrongKindCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -SyntheticAttributes @{
      $repositoryRoot = $directory
      $localRoot = $directory
      $contextPath = $directory
    }
  } catch { $wrongKindCaught = $_.Exception.Message -match 'is not a file' }
  if (-not $wrongKindCaught) { throw 'Environment probe self-test expected a wrong-kind rejection.' }
  $cases++

  $fileParentCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{
      $repositoryRoot = $directory
      $localRoot = $file
    }
  } catch { $fileParentCaught = $_.Exception.Message -match 'parent is not a directory' }
  if (-not $fileParentCaught) { throw 'Environment probe self-test expected a file-parent rejection.' }
  $cases++

  $leafInspectionErrorCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{ $repositoryRoot = $directory; $localRoot = $directory } `
      -SyntheticInspectionErrors @{ $contextPath = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'Environment probe self-test expected a leaf inspection-error rejection.' }
  $cases++

  $parentInspectionErrorCaught = $false
  try {
    Assert-EnvironmentProbePath -Path $contextPath -Root $localRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{ $repositoryRoot = $directory } `
      -SyntheticInspectionErrors @{ $localRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'Environment probe self-test expected a parent inspection-error rejection.' }
  $cases++

  Write-Output ("Environment probe path self-test: {0} cases passed." -f $cases)
}

function Assert-EnvironmentProbeDocument {
  param(
    [Parameter(Mandatory = $true)]$Document
  )

  if (-not (Test-EnvironmentProbeProperty -Object $Document -Name 'schema_version')) {
    throw 'Probe document is missing schema_version.'
  }

  if ([int]$Document.schema_version -ne 1) {
    throw 'Probe document schema_version must be 1.'
  }

  foreach ($name in @(Get-EnvironmentProbePropertyName -Value $Document)) {
    if ($ForbiddenProbeKeys -contains ([string]$name).ToLowerInvariant()) {
      throw "Probe document contains forbidden identity or private-data key: $name."
    }
  }

  $capturedAt = Get-EnvironmentProbeProperty -Object $Document -Name 'captured_at_utc'
  $parsedCapturedAt = [DateTime]::MinValue
  if ((-not $capturedAt) -or (-not [DateTime]::TryParse(
        [string]$capturedAt,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind,
        [ref]$parsedCapturedAt))) {
    throw 'Probe document captured_at_utc must be a valid timestamp.'
  }

  $os = Get-EnvironmentProbeProperty -Object $Document -Name 'os'
  if ($null -eq $os) {
    throw 'Probe document is missing os.'
  }

  $caption = Get-EnvironmentProbeProperty -Object $os -Name 'caption'
  if ([string]::IsNullOrWhiteSpace([string]$caption)) {
    throw 'Probe document os.caption must be non-empty.'
  }

  $build = Get-EnvironmentProbeProperty -Object $os -Name 'build'
  $parsedBuild = 0
  if ((-not [int]::TryParse([string]$build, [ref]$parsedBuild)) -or ($parsedBuild -lt 0)) {
    throw 'Probe document os.build must be a non-negative integer.'
  }

  $architecture = ([string](Get-EnvironmentProbeProperty -Object $os -Name 'architecture')).ToUpperInvariant()
  if (@('AMD64', 'ARM64', 'X86', 'ARM') -notcontains $architecture) {
    throw 'Probe document os.architecture is not a supported anonymous architecture value.'
  }

  $tools = Get-EnvironmentProbeProperty -Object $Document -Name 'tools'
  if ($null -eq $tools) {
    throw 'Probe document is missing tools.'
  }

  foreach ($toolName in @('cmake', 'git')) {
    if (-not (Test-EnvironmentProbeProperty -Object $tools -Name $toolName)) {
      throw "Probe document tools.$toolName is missing."
    }

    $toolVersion = Get-EnvironmentProbeProperty -Object $tools -Name $toolName
    if ($null -ne $toolVersion) {
      if ([string]::IsNullOrWhiteSpace([string]$toolVersion)) {
        throw "Probe document tools.$toolName cannot be empty when present."
      }

      if ([string]$toolVersion -match '(?i)^[A-Za-z]:[\\/]' -or [string]$toolVersion -match '(?i)[\\/](Users|home|AppData|private)[\\/]') {
        throw "Probe document tools.$toolName contains a private path."
      }
    }
  }

  $audio = Get-EnvironmentProbeProperty -Object $Document -Name 'audio'
  if ($null -eq $audio) {
    throw 'Probe document is missing audio.'
  }

  $note = [string](Get-EnvironmentProbeProperty -Object $audio -Name 'note')
  if (($note -notmatch '(?i)endpoint') -or ($note -notmatch '(?i)private calibration')) {
    throw 'Probe document audio.note must state the anonymous privacy boundary.'
  }

  $formats = @(Get-EnvironmentProbeProperty -Object $audio -Name 'supported_formats')
  if (@($formats).Count -lt 1) {
    throw 'Probe document audio.supported_formats must contain at least one format.'
  }

  foreach ($format in $formats) {
    if ($ExpectedProbeFormats -notcontains [string]$format) {
      throw "Probe document contains unsupported audio format: $format."
    }
  }

  if (@(@($formats) | Select-Object -Unique).Count -ne @($formats).Count) {
    throw 'Probe document audio.supported_formats contains duplicates.'
  }

  $json = $Document | ConvertTo-Json -Depth 10
  if ($json -match '(?i)([A-Z]:[\\/]|[\\/](Users|home|AppData|private)[\\/])') {
    throw 'Probe document contains private path-like text.'
  }

  $secretMarkers = @(
    ('BEGIN ' + ('PRIVATE' + ' KEY')),
    ('gh' + ('p' + '_')),
    ('github' + ('pat' + '_')),
    ('GUMROAD' + ('_' + 'ACCESS' + '_' + 'TOKEN')),
    ('PARTNER' + ('_' + 'CENTER' + '_' + 'TOKEN'))
  )
  foreach ($marker in $secretMarkers) {
    if ($json.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
      throw 'Probe document contains secret-like text.'
    }
  }
}

function Invoke-EnvironmentProbeSelfTest {
  $valid = New-EnvironmentProbeDocument `
    -OsCaption 'Windows test fixture' `
    -OsBuild 26100 `
    -Architecture 'AMD64' `
    -CmakeVersion 'cmake version 4.4.0' `
    -GitVersion 'git version 2.51.0' `
    -SupportedFormats $ExpectedProbeFormats `
    -CapturedAtUtc '2026-08-22T00:00:00.0000000Z'

  $missingSchema = Copy-EnvironmentProbeDocument -Document $valid
  $missingSchema.Remove('schema_version') | Out-Null

  $badSchema = Copy-EnvironmentProbeDocument -Document $valid
  $badSchema.schema_version = 2

  $badTimestamp = Copy-EnvironmentProbeDocument -Document $valid
  $badTimestamp.captured_at_utc = 'not-a-timestamp'

  $missingOsField = Copy-EnvironmentProbeDocument -Document $valid
  $missingOsField.os.Remove('caption') | Out-Null

  $missingToolField = Copy-EnvironmentProbeDocument -Document $valid
  $missingToolField.tools.Remove('git') | Out-Null

  $missingAudio = Copy-EnvironmentProbeDocument -Document $valid
  $missingAudio.Remove('audio') | Out-Null

  $badFormat = Copy-EnvironmentProbeDocument -Document $valid
  $badFormat.audio.supported_formats = @('LPCM 2.0', 'FLAC')

  $forbiddenIdentityKey = Copy-EnvironmentProbeDocument -Document $valid
  $forbiddenIdentityKey.endpoint_id = 'fixture-only'

  $forbiddenPrivateKey = Copy-EnvironmentProbeDocument -Document $valid
  $forbiddenPrivateKey.audio.private_path = 'fixture-only'

  $secretLikeText = Copy-EnvironmentProbeDocument -Document $valid
  $secretLikeText.audio.note = 'Capture endpoint metadata; ' + ('BEGIN ' + ('PRIVATE' + ' KEY'))

  $privatePathText = Copy-EnvironmentProbeDocument -Document $valid
  $privatePathText.tools.cmake = 'C:\Users\fixture\cmake.exe'

  $cases = @(
    [pscustomobject]@{ name = 'valid anonymous schema'; document = $valid; expected = $true }
    [pscustomobject]@{ name = 'missing schema field'; document = $missingSchema; expected = $false }
    [pscustomobject]@{ name = 'unsupported schema version'; document = $badSchema; expected = $false }
    [pscustomobject]@{ name = 'malformed timestamp'; document = $badTimestamp; expected = $false }
    [pscustomobject]@{ name = 'missing required os field'; document = $missingOsField; expected = $false }
    [pscustomobject]@{ name = 'missing required tool field'; document = $missingToolField; expected = $false }
    [pscustomobject]@{ name = 'missing required audio field'; document = $missingAudio; expected = $false }
    [pscustomobject]@{ name = 'unsupported audio format'; document = $badFormat; expected = $false }
    [pscustomobject]@{ name = 'forbidden identity key'; document = $forbiddenIdentityKey; expected = $false }
    [pscustomobject]@{ name = 'forbidden private-data key'; document = $forbiddenPrivateKey; expected = $false }
    [pscustomobject]@{ name = 'secret-like text'; document = $secretLikeText; expected = $false }
    [pscustomobject]@{ name = 'private path-like text'; document = $privatePathText; expected = $false }
  )

  $passed = 0
  foreach ($case in $cases) {
    $actual = $true
    $failureMessage = $null
    try {
      Assert-EnvironmentProbeDocument -Document $case.document
    }
    catch {
      $actual = $false
      $failureMessage = $_.Exception.Message
    }

    if ($actual -ne $case.expected) {
      throw "Environment probe self-test case failed: $($case.name): $failureMessage"
    }

    $passed++
  }

  Write-Output ("Environment probe self-test: {0}/{1} cases passed." -f $passed, @($cases).Count)
}

if ($SelfTest) {
  Invoke-EnvironmentProbeSelfTest
  Invoke-EnvironmentProbePathSelfTest
  exit 0
}

Assert-EnvironmentProbePath -Path $local -Root $repo -Kind Directory -AllowMissingLeaf
$contextPath = Join-Path $local 'context.json'
Assert-EnvironmentProbePath -Path $contextPath -Root $local -Kind File -AllowMissingLeaf
New-Item -ItemType Directory -Path $local -Force | Out-Null
$os = Get-CimInstance Win32_OperatingSystem
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$git = Get-Command git -ErrorAction SilentlyContinue

$probe = New-EnvironmentProbeDocument `
  -OsCaption $os.Caption `
  -OsBuild ([int]$os.BuildNumber) `
  -Architecture $env:PROCESSOR_ARCHITECTURE `
  -CmakeVersion $(if ($cmake) { & cmake --version | Select-Object -First 1 } else { $null }) `
  -GitVersion $(if ($git) { & git --version } else { $null }) `
  -SupportedFormats $ExpectedProbeFormats `
  -CapturedAtUtc ([DateTime]::UtcNow.ToString('o'))
Assert-EnvironmentProbeDocument -Document $probe

$probe | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $contextPath -Encoding utf8
Write-Output "Wrote local environment fingerprint: $contextPath"
