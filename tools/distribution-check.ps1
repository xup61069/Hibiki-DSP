[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

function Get-ProfileScalar([hashtable]$values, [string]$key, [string]$sourceName) {
  if (-not $values.ContainsKey($key)) { throw "Distribution profile missing required key [$key]: $sourceName" }
  return $values[$key]
}

function Read-DistributionProfile([string]$text, [string]$sourceName) {
  $rootAllowed = @('schema_version', 'distribution_id', 'platform', 'identities')
  $sectionAllowed = @{
    platform = @('os_min_build', 'architecture', 'target')
    identities = @(
      'driver_hardware_id', 'driver_service', 'endpoint_main_guid', 'endpoint_low_latency_guid',
      'endpoint_surround_guid', 'endpoint_virtual_mic_guid', 'volume_ui_context_guid',
      'volume_safety_context_guid', 'volume_scene_context_guid', 'volume_session_context_guid',
      'asio_clsid', 'ipc_namespace', 'ipc_control_pipe', 'asio_transport_shared_memory', 'schema_version'
    )
  }
  $values = @{}
  $seen = @{}
  $currentSection = $null
  $lineNumber = 0

  foreach ($line in ($text -split "`r?`n")) {
    $lineNumber++
    $trimmed = $line.Trim()
    if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }

    if ($line -match '^(?<indent> *)(?<key>[A-Za-z][A-Za-z0-9_]*)\s*:\s*(?<value>.*)$') {
      $indent = $matches['indent'].Length
      $key = $matches['key']
      $value = $matches['value'].Trim()
      if (($indent -ne 0) -and ($indent -ne 2)) {
        throw "Unsupported indentation at $sourceName line $lineNumber."
      }

      if ($indent -eq 0) {
        if ($rootAllowed -notcontains $key) { throw "Unknown root key [$key] at $sourceName line $lineNumber." }
        if ($seen.ContainsKey($key)) { throw "Duplicate root key [$key] at $sourceName line $lineNumber." }
        $seen[$key] = $true
        if ($key -in @('platform', 'identities')) {
          if ($value) { throw "Section [$key] must not have a scalar value at $sourceName line $lineNumber." }
          $currentSection = $key
          continue
        }
        if (-not $value -or $value -match '\s') {
          throw "Root scalar [$key] must be one bounded token at $sourceName line $lineNumber."
        }
        $values[$key] = $value
        $currentSection = $null
        continue
      }

      if ($null -eq $currentSection) { throw "Nested key [$key] has no section at $sourceName line $lineNumber." }
      if ($sectionAllowed[$currentSection] -notcontains $key) {
        throw "Unknown [$currentSection] key [$key] at $sourceName line $lineNumber."
      }
      $path = "$currentSection.$key"
      if ($seen.ContainsKey($path)) { throw "Duplicate key [$path] at $sourceName line $lineNumber." }
      if (-not $value -or $value -match '\s') {
        throw "Profile scalar [$path] must be one bounded token at $sourceName line $lineNumber."
      }
      $seen[$path] = $true
      $values[$path] = $value
      continue
    }

    throw "Unsupported distribution profile syntax at $sourceName line $lineNumber."
  }

  foreach ($key in @('schema_version', 'distribution_id')) { [void](Get-ProfileScalar $values $key $sourceName) }
  foreach ($section in $sectionAllowed.Keys) {
    foreach ($key in $sectionAllowed[$section]) {
      [void](Get-ProfileScalar $values "$section.$key" $sourceName)
    }
  }
  return ,$values
}

function Assert-DistributionProfile([hashtable]$values, [string]$sourceName) {
  if ((Get-ProfileScalar $values 'schema_version' $sourceName) -ne '1') {
    throw "Distribution schema version must remain 1: $sourceName"
  }
  if ((Get-ProfileScalar $values 'distribution_id' $sourceName) -ne 'hibiki-public-2026') {
    throw "Distribution ID drifted from hibiki-public-2026: $sourceName"
  }

  $platformExpected = @{
    'platform.os_min_build' = '26100'
    'platform.architecture' = 'x64'
    'platform.target' = 'windows-11'
  }
  foreach ($entry in $platformExpected.GetEnumerator()) {
    if ((Get-ProfileScalar $values $entry.Key $sourceName) -ne $entry.Value) {
      throw "Distribution platform value drifted for $($entry.Key): $sourceName"
    }
  }

  $identityExpected = @{
    'identities.driver_hardware_id' = 'Root\\HibikiDSP'
    'identities.driver_service' = 'HibikiVirtualAudio'
    'identities.ipc_namespace' = 'Local\\HibikiDSP\\v1'
    'identities.ipc_control_pipe' = 'HibikiDSP_v1_control'
    'identities.asio_transport_shared_memory' = 'Local\\HibikiDSP_v1_asio'
  }
  foreach ($entry in $identityExpected.GetEnumerator()) {
    if ((Get-ProfileScalar $values $entry.Key $sourceName) -ne $entry.Value) {
      throw "Distribution identity drifted for $($entry.Key): $sourceName"
    }
  }

  $guidNames = @(
    'endpoint_main_guid', 'endpoint_low_latency_guid', 'endpoint_surround_guid', 'endpoint_virtual_mic_guid',
    'volume_ui_context_guid', 'volume_safety_context_guid', 'volume_scene_context_guid',
    'volume_session_context_guid', 'asio_clsid'
  )
  $guidValues = @()
  foreach ($name in $guidNames) {
    $value = Get-ProfileScalar $values "identities.$name" $sourceName
    if ($value -notmatch '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$') {
      throw "Invalid GUID [$name]: $sourceName"
    }
    $guidValues += $value.ToLowerInvariant()
  }
  if (($guidValues | Sort-Object -Unique).Count -ne $guidValues.Count) {
    throw "Distribution GUIDs must be unique: $sourceName"
  }

  if ((Get-ProfileScalar $values 'identities.schema_version' $sourceName) -ne '1') {
    throw "Identity schema version must remain 1: $sourceName"
  }
}

if ($SelfTest) {
  $valid = @'
# bounded DistributionProfile v1 fixture
schema_version: 1
distribution_id: hibiki-public-2026
platform:
  os_min_build: 26100
  architecture: x64
  target: windows-11
identities:
  driver_hardware_id: Root\\HibikiDSP
  driver_service: HibikiVirtualAudio
  endpoint_main_guid: 8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10
  endpoint_low_latency_guid: 6d5706a4-b661-4bf6-9c2d-9c31b8f7df21
  endpoint_surround_guid: d4a21e0f-83e5-4a6e-92dc-44d13f9e6c93
  endpoint_virtual_mic_guid: 5e90de25-0e88-4892-8b0e-a1d521cb3f40
  volume_ui_context_guid: 5b1fbad1-8e7c-4e8a-910d-2a654f937c11
  volume_safety_context_guid: 7c3c2e54-1a5f-4c2a-a834-5d76812b4e90
  volume_scene_context_guid: 8f3d9b66-2c11-4fd0-b742-1e63559ac428
  volume_session_context_guid: a4e2c779-3d88-421b-9c0a-724d18ef6b35
  asio_clsid: 2c0e8d4f-7d9f-4aa7-a03d-690dcf5d5f8c
  ipc_namespace: Local\\HibikiDSP\\v1
  ipc_control_pipe: HibikiDSP_v1_control
  asio_transport_shared_memory: Local\\HibikiDSP_v1_asio
  schema_version: 1
'@
  # Keep fixture replacements independent of the host checkout's CRLF/LF policy.
  $valid = $valid -replace "`r`n", "`n"
  $validValues = Read-DistributionProfile $valid 'selftest-valid.yml'
  Assert-DistributionProfile $validValues 'selftest-valid.yml'

  $fixtures = @(
    @{ Name = 'duplicate-root-key'; Text = $valid.Replace("schema_version: 1`n", "schema_version: 1`nschema_version: 1`n") },
    @{ Name = 'unknown-key'; Text = $valid.Replace("platform:`n", "unexpected: 1`nplatform:`n") },
    @{ Name = 'unknown-nested-key'; Text = $valid.Replace("  target: windows-11", "  unexpected: windows-11") },
    @{ Name = 'bad-indentation'; Text = $valid.Replace("  os_min_build: 26100", "   os_min_build: 26100") },
    @{ Name = 'missing-required-field'; Text = $valid.Replace("  architecture: x64`n", '') },
    @{ Name = 'invalid-schema'; Text = $valid.Replace("schema_version: 1`n", "schema_version: 2`n") },
    @{ Name = 'invalid-platform'; Text = $valid.Replace('os_min_build: 26100', 'os_min_build: 26000') },
    @{ Name = 'invalid-guid'; Text = $valid.Replace('8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10', 'not-a-guid') },
    @{ Name = 'duplicate-guid'; Text = $valid.Replace('6d5706a4-b661-4bf6-9c2d-9c31b8f7df21', '8b9b2a8f-09a4-4e57-9f24-5d7cbd50ce10') },
    @{ Name = 'identity-drift'; Text = $valid.Replace('HibikiDSP_v1_control', 'OtherDSP_control') }
  )
  foreach ($fixture in $fixtures) {
    $caught = $false
    try {
      $fixtureValues = Read-DistributionProfile $fixture.Text "selftest-$($fixture.Name).yml"
      Assert-DistributionProfile $fixtureValues "selftest-$($fixture.Name).yml"
    } catch { $caught = $true }
    if (-not $caught) { throw "Distribution profile self-test expected rejection: $($fixture.Name)" }
  }
  Write-Output "Distribution profile self-test passed ($($fixtures.Count + 1) cases)."
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$path = Join-Path $repo 'config/distribution-profile.yml'
if (-not (Test-Path -LiteralPath $path)) { throw 'Missing config/distribution-profile.yml.' }
$values = Read-DistributionProfile (Get-Content -LiteralPath $path -Raw) $path
Assert-DistributionProfile $values $path
Write-Output 'Distribution profile checks passed.'
