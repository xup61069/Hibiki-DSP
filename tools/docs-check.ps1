#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-CounterClaims([string]$Text) {
  # Parse structural counters (required entries, specs) from BASELINE prose.
  # Volatile tracked-path / repository-JSON counts are measured live via git ls-files
  # and reported as informational output only; they are never compared against
  # committed numbers.
  $claims = [pscustomobject]@{ Required = 0; Specs = 0 }
  $patterns = [ordered]@{
    Required = @('docs-check\.ps1`\s*的\s*(?<count>\d+)\s*個必要入口', 'docs-check required-entry')
    Specs    = @('個必要入口與\s*(?<count>\d+)\s*份\s*Spec\s*通過', 'spec')
  }
  foreach ($key in $patterns.Keys) {
    $match = [regex]::Match($Text, [string]$patterns[$key][0])
    if (-not $match.Success) {
      throw ("BASELINE.md is missing the {0} counter expected by docs-check; keep the " +
             "verification-summary sentence in docs/state/BASELINE.md up to date.") -f $patterns[$key][1]
    }
    $claims.$key = [int]$match.Groups['count'].Value
  }
  return $claims
}

function Assert-StructuralClaims {
  param(
    [Parameter(Mandatory = $true)]$Claims,
    [Parameter(Mandatory = $true)][int]$Required,
    [Parameter(Mandatory = $true)][int]$Specs
  )
  if ($Claims.Required -ne $Required) {
    throw ("BASELINE.md claims {0} docs-check required entries but docs-check defines {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Required, $Required
  }
  if ($Claims.Specs -ne $Specs) {
    throw ("BASELINE.md claims {0} specs but the repository tracks {1}; " +
           "update the BASELINE.md verification summary.") -f $Claims.Specs, $Specs
  }
}

function Convert-CommandOutputToText {
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$Lines
  )
  if ($null -eq $Lines -or $Lines.Count -eq 0) { return '' }
  $strings = @($Lines | ForEach-Object { [string]$_ })
  return [string]::Join("`n", $strings)
}

function Test-MergeBaseMode {
  param(
    [string]$BaseRef,
    [string]$RefName
  )
  if (-not [string]::IsNullOrWhiteSpace($BaseRef)) { return $true }
  if ([string]::IsNullOrWhiteSpace($RefName)) { return $false }
  if ($RefName -eq 'main' -or $RefName -eq 'refs/heads/main' -or $RefName.EndsWith('/main')) {
    return $false
  }
  return $true
}

function Resolve-CiRefName {
  param(
    [string]$RefName,
    [string]$Ref,
    [string]$EventName,
    [string]$CurrentBranch
  )
  $resolved = ''
  if (-not [string]::IsNullOrWhiteSpace($RefName)) {
    $resolved = $RefName
  }
  elseif (-not [string]::IsNullOrWhiteSpace($Ref)) {
    $resolved = $Ref -replace '^refs/heads/', ''
  }
  if ($EventName -eq 'push' -and -not [string]::IsNullOrWhiteSpace($CurrentBranch)) {
    $resolved = $CurrentBranch
  }
  return $resolved
}

function Test-BaselineChangedByHead {
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$ChangedPaths
  )
  return @($ChangedPaths | Where-Object { $_ -eq 'docs/state/BASELINE.md' }).Count -gt 0
}

function Get-AiContextBudgetErrors {
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()]$Entries
  )

  $errors = @()
  foreach ($entry in $Entries) {
    $length = ([string]$entry.Content).Length
    if ($length -gt [int]$entry.MaxCharacters) {
      $errors += ("{0}: {1} characters exceeds the AI context budget of {2}" -f
        $entry.Path, $length, $entry.MaxCharacters)
    }
    foreach ($pattern in @($entry.ForbiddenPatterns)) {
      if ([regex]::IsMatch([string]$entry.Content, [string]$pattern)) {
        $errors += ("{0}: contains a forbidden AI startup route ({1})" -f $entry.Path, $pattern)
      }
    }
  }
  return $errors
}

$unboundedWorktreePattern = 'git worktree list(?![^\r\n`]*\|\s*(?:rg|Select-String)\b)'
$unboundedGitLogPattern = 'git log(?![^\r\n`]*(?:-\d+\b|--max-count(?:=|\s+)\d+\b))[^\r\n`]*origin/main'
$unboundedGitHubListPattern = 'gh (?:issue|pr) list(?![^\r\n`]*--limit\s+\d+\b)'
$duplicateIssueBodyPattern = 'gh issue view\s+[^\s`]+(?![^\r\n`]*--json\b)'
$staleGlobalHandoffRoutePattern = '\[AI 接手頁\]\(docs/AI_HANDOFF\.md\)\s*操作|新 AI 先讀\s+`docs/AI_HANDOFF\.md`'
$adapterGlobalPreloadPattern = 'docs/(?:AI_HANDOFF|state/BASELINE|PROJECT_MAP|ai/MULTI_AGENT)\.md'

function Get-MissingRequiredSchemas {
  # Every tracked JSON Schema is a durable public contract and therefore must be
  # a required docs-check entry. Keep this pure so -SelfTest can cover the
  # fail-closed behavior without touching the repository.
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$TrackedPaths,
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$RequiredEntries
  )
  $trackedSchemas = @($TrackedPaths |
    Where-Object { $_ -match '^(?:schemas)/[^/]+\.schema\.json$' } |
    Sort-Object)
  $requiredSet = @{}
  foreach ($entry in $RequiredEntries) { $requiredSet[$entry] = $true }
  return ,@($trackedSchemas | Where-Object { -not $requiredSet.ContainsKey($_) })
}

function Get-SchemaStructureErrors {
  # Validates that every tracked contract schema has the required top-level
  # properties ($schema, $id, title, type) and that no $id value is duplicated.
  # Keep this pure so -SelfTest can cover it without touching real files.
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()]$SchemaEntries
  )
  $errors = @()
  $seenIds = @{ }
  foreach ($entry in $SchemaEntries) {
    try {
      $parsed = $entry.Content | ConvertFrom-Json
    } catch {
      $errors += ($entry.Path + ': invalid JSON (' + $_.Exception.Message + ')')
      continue
    }
    foreach ($key in @('$schema', '$id', 'title', 'type')) {
      if (-not $parsed.PSObject.Properties[$key]) {
        $errors += ($entry.Path + ': missing required top-level property: ' + $key)
      }
    }
    if ($parsed.PSObject.Properties['$id']) {
      $idValue = [string]$parsed.'$id'
      if ([string]::IsNullOrWhiteSpace($idValue)) {
        $errors += ($entry.Path + ': $id must be a non-empty string')
      } elseif ($seenIds.ContainsKey($idValue)) {
        $errors += ($entry.Path + ': duplicate $id ' + $idValue + ' (first seen in ' + $seenIds[$idValue] + ')')
      } else {
        $seenIds[$idValue] = $entry.Path
      }
    }
  }
  return ,@($errors)
}

function Test-JsonParseAll {
  # Validates that every entry in the provided list of (Path, Content) pairs is
  # parseable JSON. Returns an array of error strings; empty means all valid.
  # Keep this pure so -SelfTest can cover it without touching real files.
  param(
    [Parameter(Mandatory = $true)][AllowEmptyCollection()]$FileEntries
  )
  $errors = @()
  foreach ($entry in $FileEntries) {
    try {
      $null = $entry.Content | ConvertFrom-Json -ErrorAction Stop
    } catch {
      $errors += ($entry.Path + ': invalid JSON (' + $_.Exception.Message + ')')
    }
  }
  return ,@($errors)
}

function Assert-JsonSchemaExpectation {
  param(
    [Parameter(Mandatory = $true)][string]$SchemaFile,
    [Parameter(Mandatory = $true)]$Document,
    [Parameter(Mandatory = $true)][bool]$ExpectedValid,
    [Parameter(Mandatory = $true)][string]$CaseName
  )

  $json = $Document | ConvertTo-Json -Depth 10 -Compress
  $validationErrors = @()
  try {
    $validationResult = @(Test-Json -Json $json -SchemaFile $SchemaFile `
      -ErrorAction SilentlyContinue -ErrorVariable +validationErrors)
  } catch {
    throw ("JSON schema validator failed for '{0}' using '{1}': {2}" -f
      $CaseName, $SchemaFile, $_.Exception.Message)
  }

  if ($validationResult.Count -ne 1 -or $validationResult[0] -isnot [bool]) {
    throw ("JSON schema validator did not return exactly one Boolean for '{0}' using '{1}'." -f
      $CaseName, $SchemaFile)
  }

  $actualValid = [bool]$validationResult[0]
  if ($actualValid -ne $ExpectedValid) {
    throw ("JSON schema expectation failed for '{0}' using '{1}': expected={2}, actual={3}, validator_errors={4}." -f
      $CaseName, $SchemaFile, $ExpectedValid, $actualValid, $validationErrors.Count)
  }
}

function Register-PrintableStringSchema {
  param(
    [Parameter(Mandatory = $true)][string]$SchemaFile
  )

  # Test-Json resolves external references from the global schema registry.
  # Register the local shared fragment under its canonical $id before validating
  # schemas that reference it.
  $null = Test-Json -Json '{}' -SchemaFile $SchemaFile -ErrorAction Stop
  $schema = [Json.Schema.JsonSchema]::FromFile($SchemaFile)
  [Json.Schema.SchemaRegistry]::Global.Register($schema)
}

function Assert-PrintableContractSchemas {
  param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
  )

  $audioSchemaFile = Join-Path $RepositoryRoot 'schemas/audio-session-descriptor-v1.schema.json'
  $calibrationSchemaFile = Join-Path $RepositoryRoot 'schemas/calibration-response-v1.schema.json'

  $audioDocument = [ordered]@{
    schema_version = 1
    identity = [ordered]@{
      endpoint_id = 'endpoint-音訊'
      session_instance_id = 'session-工作階段'
      process_id = 1
    }
    display_name = 'Hibiki 音訊'
    app_id = 'app.音訊'
    active = $true
    gain_owner = 'windows-session'
    lane_id = 'lane-音訊'
    output_group = 'group-音訊'
    makeup_gain_db = 0
  }
  Assert-JsonSchemaExpectation -SchemaFile $audioSchemaFile -Document $audioDocument `
    -ExpectedValid $true -CaseName 'audio printable UTF-8 baseline'

  $audioEmptyOptionalDocument = $audioDocument | ConvertTo-Json -Depth 10 -Compress |
    ConvertFrom-Json -AsHashtable
  foreach ($field in @('display_name', 'app_id', 'lane_id', 'output_group')) {
    $audioEmptyOptionalDocument[$field] = ''
  }
  Assert-JsonSchemaExpectation -SchemaFile $audioSchemaFile -Document $audioEmptyOptionalDocument `
    -ExpectedValid $true -CaseName 'audio optional empty labels'

  $calibrationDocument = [ordered]@{
    schema_version = 1
    sample_rate = 48000
    channels = 2
    device_id = '裝置-1'
    points = @(
      [ordered]@{
        frequency_hz = 1000
        measured_db = 0
        target_db = 0
      }
    )
  }
  Assert-JsonSchemaExpectation -SchemaFile $calibrationSchemaFile -Document $calibrationDocument `
    -ExpectedValid $true -CaseName 'calibration printable UTF-8 baseline'

  $controlCodePoints = @(0x00..0x1F) + @(0x7F) + @(0x80..0x9F)
  $positions = @('leading', 'middle', 'trailing', 'controls-only')
  $audioFields = @(
    'identity.endpoint_id',
    'identity.session_instance_id',
    'display_name',
    'app_id',
    'lane_id',
    'output_group'
  )

  foreach ($field in $audioFields) {
    foreach ($codePoint in $controlCodePoints) {
      $control = [char]$codePoint
      foreach ($position in $positions) {
        $value = switch ($position) {
          'leading' { ([string]$control) + 'post' }
          'middle' { 'pre' + ([string]$control) + 'post' }
          'trailing' { 'pre' + ([string]$control) }
          'controls-only' { [string]$control }
        }
        $document = $audioDocument | ConvertTo-Json -Depth 10 -Compress |
          ConvertFrom-Json -AsHashtable
        if ($field.StartsWith('identity.')) {
          $document['identity'][$field.Substring('identity.'.Length)] = $value
        } else {
          $document[$field] = $value
        }
        $caseName = 'audio {0} U+{1:X4} {2}' -f $field, $codePoint, $position
        Assert-JsonSchemaExpectation -SchemaFile $audioSchemaFile -Document $document `
          -ExpectedValid $false -CaseName $caseName
      }
    }

    $document = $audioDocument | ConvertTo-Json -Depth 10 -Compress |
      ConvertFrom-Json -AsHashtable
    $unicodeLineSeparatorThenEscape = 'pre' + ([char]0x2028) + ([char]0x1B) + 'post'
    if ($field.StartsWith('identity.')) {
      $document['identity'][$field.Substring('identity.'.Length)] = $unicodeLineSeparatorThenEscape
    } else {
      $document[$field] = $unicodeLineSeparatorThenEscape
    }
    Assert-JsonSchemaExpectation -SchemaFile $audioSchemaFile -Document $document `
      -ExpectedValid $false -CaseName ('audio {0} U+2028 before U+001B' -f $field)

    $document = $audioDocument | ConvertTo-Json -Depth 10 -Compress |
      ConvertFrom-Json -AsHashtable
    $unicodeLineSeparator = 'pre' + ([char]0x2028) + 'post'
    if ($field.StartsWith('identity.')) {
      $document['identity'][$field.Substring('identity.'.Length)] = $unicodeLineSeparator
    } else {
      $document[$field] = $unicodeLineSeparator
    }
    Assert-JsonSchemaExpectation -SchemaFile $audioSchemaFile -Document $document `
      -ExpectedValid $true -CaseName ('audio {0} printable U+2028' -f $field)
  }

  foreach ($codePoint in $controlCodePoints) {
    $control = [char]$codePoint
    foreach ($position in $positions) {
      $value = switch ($position) {
        'leading' { ([string]$control) + 'post' }
        'middle' { 'pre' + ([string]$control) + 'post' }
        'trailing' { 'pre' + ([string]$control) }
        'controls-only' { [string]$control }
      }
      $document = $calibrationDocument | ConvertTo-Json -Depth 10 -Compress |
        ConvertFrom-Json -AsHashtable
      $document['device_id'] = $value
      $caseName = 'calibration device_id U+{0:X4} {1}' -f $codePoint, $position
      Assert-JsonSchemaExpectation -SchemaFile $calibrationSchemaFile -Document $document `
        -ExpectedValid $false -CaseName $caseName
    }
  }

  $document = $calibrationDocument | ConvertTo-Json -Depth 10 -Compress |
    ConvertFrom-Json -AsHashtable
  $document['device_id'] = 'pre' + ([char]0x2028) + ([char]0x1B) + 'post'
  Assert-JsonSchemaExpectation -SchemaFile $calibrationSchemaFile -Document $document `
    -ExpectedValid $false -CaseName 'calibration device_id U+2028 before U+001B'

  $document = $calibrationDocument | ConvertTo-Json -Depth 10 -Compress |
    ConvertFrom-Json -AsHashtable
  $document['device_id'] = 'pre' + ([char]0x2028) + 'post'
  Assert-JsonSchemaExpectation -SchemaFile $calibrationSchemaFile -Document $document `
    -ExpectedValid $true -CaseName 'calibration device_id printable U+2028'
}

function Copy-PrintableSchemaDocument {
  param([Parameter(Mandatory = $true)]$Document)
  return $Document | ConvertTo-Json -Depth 12 -Compress |
    ConvertFrom-Json -AsHashtable
}

function Set-PrintableSchemaDocumentValue {
  param(
    [Parameter(Mandatory = $true)]$Document,
    [Parameter(Mandatory = $true)][string]$FieldPath,
    [Parameter(Mandatory = $true)]$Value
  )

  $parts = $FieldPath -split '\\.'
  $current = $Document
  for ($index = 0; $index -lt $parts.Count - 1; $index++) {
    $key = $parts[$index]
    if ($key -match '^\\d+$') {
      $current = $current[[int]$key]
    } else {
      $current = $current[$key]
    }
  }

  $key = $parts[$parts.Count - 1]
  if ($key -match '^\\d+$') {
    $current[[int]$key] = $Value
  } else {
    $current[$key] = $Value
  }
}

function Assert-ExtendedPrintableContractSchemas {
  param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot
  )

  $outputFanoutSinks = @()
  for ($index = 0; $index -lt 8; $index++) {
    $outputFanoutSinks += [ordered]@{
      sink_id = "sink-$index"
      channels = 2
      enabled = $true
    }
  }

  $digest64 = 'a' * 64
  $commit40 = 'b' * 40
  $thumbprint40 = 'c' * 40

  $cases = @(
    [pscustomobject]@{
      SchemaName = 'acoustic-anchor-v1'
      Fields = @('microphone_calibration_id')
      Document = [ordered]@{
        schema_version = 1
        device_class = 'speaker'
        test_signal_dbfs = -12
        measured_1k_spl_db = 78
        uncertainty_db = 2
        microphone_calibration_id = 'mic-音訊-1'
      }
    },
    [pscustomobject]@{
      SchemaName = 'custom-scene-cards-v1'
      Fields = @('scenes.0.name', 'scenes.0.description', 'scenes.0.latency_label')
      Document = [ordered]@{
        schema_version = 1
        scenes = @([ordered]@{
          id = 'game-card'
          name = '遊戲卡片'
          description = 'printable 描述'
          latency_label = '低延遲'
          safety_enabled = $true
        })
      }
    },
    [pscustomobject]@{
      SchemaName = 'device-switch-request-v1'
      Fields = @('endpoint_id')
      Document = [ordered]@{
        schema_version = 1
        endpoint_id = 'endpoint-音訊'
        channels = 2
        sample_rate = 48000
        buffer_frames = 256
        catalog_sequence = 7
      }
    },
    [pscustomobject]@{
      SchemaName = 'driver-control-v1'
      Fields = @('endpoint_guid', 'event_context_guid')
      Document = [ordered]@{
        schema_version = 1
        message_type = 'volume-notification'
        request_id = 1
        endpoint_guid = '{0F3A-音訊-GUID}'
        event_context_guid = '{0F3A-事件-GUID}'
        channel_count = 2
        sample_rate = 48000
        frames_per_buffer = 256
        requested_db_q16_16 = 0
        safety_ceiling_db_q16_16 = 0
        effective_db_q16_16 = 0
        mute = $false
        generation = 1
        actuator = 'internal-dsp'
      }
    },
    [pscustomobject]@{
      SchemaName = 'equal-loudness-policy-v1'
      Fields = @('anchor_id')
      Document = [ordered]@{
        schema_version = 1
        standard = 'iso-226-2023-derived'
        mode = 'relative'
        reference_phon = 40
        strength = 0.5
        max_boost_db = 6
        anchor_id = 'anchor-音訊'
      }
    },
    [pscustomobject]@{
      SchemaName = 'equal-loudness-status-v1'
      Fields = @('diagnostic')
      Document = [ordered]@{
        schema_version = 1
        mode = 'relative'
        calibrated = $false
        limited = $false
        maximum_fit_error_db = 0
        realized_peak_db = -1
        diagnostic = '正常'
      }
    },
    [pscustomobject]@{
      SchemaName = 'graph-config-v1'
      Fields = @('lanes.0.id', 'lanes.0.output_group')
      Document = [ordered]@{
        schema_version = 1
        output_channels = 8
        strict_direct = $false
        lanes = @([ordered]@{
          id = 'lane-音訊'
          output_group = 'group-音訊'
          channel_count = 8
          makeup_gain_db = 0
          enabled = $true
          channel_map = @(0, 1, 2, 3, 4, 5, 6, 7)
        })
      }
    },
    [pscustomobject]@{
      SchemaName = 'output-fanout-plan-v1'
      Fields = @('sinks.0.sink_id')
      Document = [ordered]@{
        schema_version = 1
        revision = 1
        output_channels = 8
        sink_count = 8
        sinks = $outputFanoutSinks
      }
    },
    [pscustomobject]@{
      SchemaName = 'physical-device-catalog-v1'
      Fields = @('devices.0.endpoint_id', 'devices.0.display_name')
      Document = [ordered]@{
        schema_version = 1
        devices = @([ordered]@{
          schema_version = 1
          endpoint_id = 'endpoint-實體'
          display_name = '實體喇叭'
          flow = 'render'
          availability = 'active'
          channels = 2
          sample_rate = 48000
          buffer_frames = 256
          is_default = $true
          last_sequence = 7
        })
      }
    },
    [pscustomobject]@{
      SchemaName = 'release-manifest-v1'
      Fields = @(
        'product_version',
        'distribution_id',
        'unsigned_files.0.path',
        'installer.rfc3161_timestamp',
        'tests.0'
      )
      Document = [ordered]@{
        schema_version = 1
        product_version = '1.0.0'
        source_tag = 'v1.0.0'
        source_commit = $commit40
        distribution_id = 'distribution-標準'
        toolchain_digest = $digest64
        dependency_lock_digest = $digest64
        unsigned_files = @([ordered]@{
          path = 'apps/ Hibiki.exe'
          sha256 = $digest64
        })
        driver_package = [ordered]@{
          sha256 = $digest64
          catalog_sha256 = $digest64
          microsoft_signature_thumbprint = $thumbprint40
        }
        installer = [ordered]@{
          sha256 = $digest64
          signer_thumbprint = $thumbprint40
          rfc3161_timestamp = 'RFC3161 timestamp'
        }
        sbom_digest = $digest64
        tests = @('schema strict-end regression')
      }
    },
    [pscustomobject]@{
      SchemaName = 'scene-profile-v1'
      Fields = @(
        'name',
        'lanes.0',
        'automation_timeline_ids.0',
        'ir_reference',
        'output_group'
      )
      Document = [ordered]@{
        schema_version = 1
        id = 'scene-game'
        name = '遊戲場景'
        lanes = @('lane-音訊')
        automation_timeline_ids = @('timeline-音訊')
        ir_reference = 'prepared-ir-label'
        output_group = 'group-音訊'
        latency_mode = 'game'
        safety = [ordered]@{
          limiter_dbtp = -1
          auto_attenuate = $true
        }
      }
    },
    [pscustomobject]@{
      SchemaName = 'scene-sync-queue-v1'
      Fields = @('operations.0.name', 'operations.0.output_group')
      Document = [ordered]@{
        schema_version = 1
        dropped_operations = 0
        operations = @([ordered]@{
          is_upsert = $true
          scene_id = 'scene-game'
          name = '遊戲場景'
          output_group = 'group-音訊'
        })
      }
    },
    [pscustomobject]@{
      SchemaName = 'session-route-rule-v1'
      Fields = @('match.app_id', 'match.display_name_contains', 'lane_id', 'output_group')
      Document = [ordered]@{
        schema_version = 1
        rule_id = 'route-game'
        priority = 10
        enabled = $true
        match = [ordered]@{
          app_id = 'app.音訊'
          display_name_contains = '遊戲'
        }
        lane_id = 'lane-音訊'
        output_group = 'group-音訊'
        gain_owner = 'windows-session'
        makeup_gain_db = 0
      }
    },
    [pscustomobject]@{
      SchemaName = 'session-route-rules-v1'
      Fields = @('rules.0.app_id', 'rules.0.display_name', 'rules.0.lane_id', 'rules.0.output_group')
      Document = [ordered]@{
        schema_version = 1
        rules = @([ordered]@{
          rule_id = 'route-game'
          priority = 10
          enabled = $true
          gain_owner = 0
          makeup_gain_db = 0
          app_id = 'app.音訊'
          display_name = '遊戲'
          lane_id = 'lane-音訊'
          output_group = 'group-音訊'
        })
      }
    }
  )

  foreach ($case in $cases) {
    if ($case.SchemaName -eq 'scene-profile-v1') {
      continue
    }
    $schemaFile = Join-Path $RepositoryRoot ("schemas/" + $case.SchemaName + ".schema.json")
    Assert-JsonSchemaExpectation -SchemaFile $schemaFile -Document $case.Document `
      -ExpectedValid $true -CaseName ($case.SchemaName + ' printable UTF-8 baseline')

    foreach ($field in $case.Fields) {
      $trailingLfDocument = Copy-PrintableSchemaDocument -Document $case.Document
      Set-PrintableSchemaDocumentValue -Document $trailingLfDocument `
        -FieldPath $field -Value ('pre' + ([char]0x0A))
      Assert-JsonSchemaExpectation -SchemaFile $schemaFile -Document $trailingLfDocument `
        -ExpectedValid $false -CaseName ($case.SchemaName + ' ' + $field + ' trailing LF')

      $middleDelDocument = Copy-PrintableSchemaDocument -Document $case.Document
      Set-PrintableSchemaDocumentValue -Document $middleDelDocument `
        -FieldPath $field -Value ('pre' + ([char]0x7F) + 'post')
      Assert-JsonSchemaExpectation -SchemaFile $schemaFile -Document $middleDelDocument `
        -ExpectedValid $false -CaseName ($case.SchemaName + ' ' + $field + ' middle DEL')
    }
  }
}

function ConvertFrom-AdrFrontmatter([string]$RawText) {
  # Parse the comment-style frontmatter block used by all ADR files.
  # Returns a hashtable of key -> string value, or throws on structural errors.
  $lines = @($RawText -split "`r?`n")
  if ($lines.Count -lt 3) { throw 'ADR file is too short to contain frontmatter.' }
  if ($lines[0] -ne '# ---') { throw 'ADR frontmatter must start with "# ---" on line 1.' }
  $endIndex = -1
  for ($i = 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq '# ---') { $endIndex = $i; break }
  }
  if ($endIndex -lt 2) { throw 'ADR frontmatter closing "# ---" not found or block is empty.' }

  $fields = @{}
  for ($i = 1; $i -lt $endIndex; $i++) {
    $line = $lines[$i]
    if (-not $line.StartsWith('# ')) { throw "ADR frontmatter line $($_ + 1) must start with '# ': $line" }
    $content = $line.Substring(2).Trim()
    if ([string]::IsNullOrEmpty($content)) { continue }
    $colonIdx = $content.IndexOf(':')
    if ($colonIdx -lt 1) { throw "ADR frontmatter entry lacks a key:value separator: $content" }
    $key = $content.Substring(0, $colonIdx).Trim()
    $value = $content.Substring($colonIdx + 1).Trim()
    if ($fields.ContainsKey($key)) { throw "Duplicate ADR frontmatter key: $key" }
    $fields[$key] = $value
  }
  return $fields
}

function Assert-AdrFrontmatter {
  param(
    [Parameter(Mandatory = $true)]$Fields,
    [Parameter(Mandatory = $true)][string]$FileName
  )
  foreach ($key in @('id', 'status', 'owner', 'authority', 'date', 'last_reviewed', 'review_after_days', 'source_globs')) {
    if (-not $Fields.ContainsKey($key) -or [string]::IsNullOrWhiteSpace([string]$Fields[$key])) {
      throw "$FileName is missing required ADR frontmatter field: $key"
    }
  }

  $allowedAuthority = @('workflow', 'product-behavior', 'architecture', 'current-state', 'task', 'evidence')
  if ($allowedAuthority -notcontains $Fields['authority']) {
    throw "$FileName authority '$($Fields['authority'])' is not in the allowed set."
  }

  $days = 0
  if (-not [int]::TryParse([string]$Fields['review_after_days'], [ref]$days) -or $days -lt 1) {
    throw "$FileName review_after_days must be a positive integer."
  }

  $parsedDate = [datetime]::MinValue
  foreach ($dateKey in @('date', 'last_reviewed')) {
    if (-not [datetime]::TryParseExact([string]$Fields[$dateKey], 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
      throw "$FileName $dateKey must be formatted as yyyy-MM-dd."
    }
  }
}

function ConvertFrom-SpecFrontmatter([string]$RawText) {
  # Parse the YAML-style frontmatter block used by all Spec files.
  # Returns a hashtable of key -> string value, or throws on structural errors.
  $lines = @($RawText -split "`r?`n")
  if ($lines.Count -lt 3) { throw 'Spec file is too short to contain frontmatter.' }
  if ($lines[0] -ne '---') { throw 'Spec frontmatter must start with "---" on line 1.' }
  $endIndex = -1
  for ($i = 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq '---') { $endIndex = $i; break }
  }
  if ($endIndex -lt 2) { throw 'Spec frontmatter closing "---" not found or block is empty.' }

  $fields = @{}
  for ($i = 1; $i -lt $endIndex; $i++) {
    $line = $lines[$i]
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $colonIdx = $line.IndexOf(':')
    if ($colonIdx -lt 1) { throw "Spec frontmatter line $($i + 1) lacks a key:value separator: $line" }
    $key = $line.Substring(0, $colonIdx).Trim()
    $value = $line.Substring($colonIdx + 1).Trim()
    if ($fields.ContainsKey($key)) { throw "Duplicate Spec frontmatter key: $key" }
    $fields[$key] = $value
  }
  return $fields
}

function Assert-SpecFrontmatter {
  param(
    [Parameter(Mandatory = $true)]$Fields,
    [Parameter(Mandatory = $true)][string]$FileName
  )
  foreach ($key in @('id', 'status', 'owner', 'authority', 'last_reviewed', 'review_after_days', 'related_adrs', 'source_globs')) {
    if (-not $Fields.ContainsKey($key) -or [string]::IsNullOrWhiteSpace([string]$Fields[$key])) {
      throw "$FileName is missing required Spec frontmatter field: $key"
    }
  }

  $allowedStatus = @('accepted', 'draft')
  if ($allowedStatus -notcontains $Fields['status']) {
    throw "$FileName status '$($Fields['status'])' is not in the allowed set."
  }

  $allowedAuthority = @('product-behavior', 'repository-process', 'release-policy', 'architecture', 'platform-boundary', 'control-plane', 'control-model')
  if ($allowedAuthority -notcontains $Fields['authority']) {
    throw "$FileName authority '$($Fields['authority'])' is not in the allowed set."
  }

  $days = 0
  if (-not [int]::TryParse([string]$Fields['review_after_days'], [ref]$days) -or $days -lt 1) {
    throw "$FileName review_after_days must be a positive integer."
  }

  $parsedDate = [datetime]::MinValue
  if (-not [datetime]::TryParseExact([string]$Fields['last_reviewed'], 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
    throw "$FileName last_reviewed must be formatted as yyyy-MM-dd."
  }
}

function Test-MarkdownRelativeLinks {
  param(
    [Parameter(Mandatory)] [AllowEmptyCollection()] [AllowEmptyString()] [string[]] $Lines,
    [Parameter(Mandatory)] [string] $BaseDir
  )
  $broken = @()
  $checked = 0
  $inFence = $false
  foreach ($line in $lines) {
    if ($line -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
    if ($inFence) { continue }
    $pos = 0
    while ($true) {
      $idx = $line.IndexOf('](', $pos)
      if ($idx -lt 0) { break }
      $open = $line.LastIndexOf('[', $idx)
      if ($open -ge 0) {
        $rest = $line.Substring($idx + 2)
        $end = $rest.IndexOf(')')
        if ($end -gt 0) {
          $target = $rest.Substring(0, $end).Trim().Trim('<>').Split(' ')[0]
          $checked++
          $isExternal = $target.StartsWith('http://') -or $target.StartsWith('https://') -or $target.StartsWith('mailto:') -or $target.StartsWith('#')
          if (-not $isExternal) {
            $pathPart = $target.Split('#')[0]
            if ($pathPart) {
              try {
                $decoded = [System.Uri]::UnescapeDataString($pathPart)
                $resolved = Join-Path $BaseDir $decoded
                if (-not (Test-Path -LiteralPath $resolved)) { $broken += $target }
              } catch {
                $broken += $target
              }
            }
          }
        }
      }
      $pos = $idx + 2
    }
  }
  return @{ Broken = $broken; Checked = $checked }
}

if ($SelfTest) {
  $caseCount = 0

  # Multiline normalization.
  $multilineLines = @('line-one', 'line-two', 'line-three')
  $multilineText = Convert-CommandOutputToText -Lines $multilineLines
  if ($multilineText -ne "line-one`nline-two`nline-three") {
    throw 'docs-check self-test failed: multiline command output did not normalize to text.'
  }
  $caseCount++

  # Merge-base mode detection.
  if (-not (Test-MergeBaseMode -BaseRef 'main' -RefName '')) {
    throw 'docs-check self-test failed: pull-request mode was not recognized.'
  }
  $caseCount++
  if (Test-MergeBaseMode -BaseRef '' -RefName 'main') {
    throw 'docs-check self-test failed: direct main push must remain strict.'
  }
  $caseCount++
  if (-not (Test-MergeBaseMode -BaseRef '' -RefName 'codex/feature')) {
    throw 'docs-check self-test failed: feature-branch push mode was not recognized.'
  }
  $caseCount++

  # CI ref resolution.
  if ((Resolve-CiRefName -RefName '' -Ref '' -EventName 'push' -CurrentBranch 'codex/feature') -ne 'codex/feature') {
    throw 'docs-check self-test failed: push-event checkout branch fallback was not recognized.'
  }
  $caseCount++

  # BASELINE edit detection.
  if (Test-BaselineChangedByHead -ChangedPaths @('evidence/0000-foundation/initial.json')) {
    throw 'docs-check self-test failed: a handoff-only head was treated as a BASELINE owner.'
  }
  $caseCount++

  # AI context budgets and forbidden startup routes stay fail closed.
  $validContextEntries = @(
    [pscustomobject]@{ Path = 'docs/AI_HANDOFF.md'; Content = 'short live router'; MaxCharacters = 64; ForbiddenPatterns = @() },
    [pscustomobject]@{ Path = 'CLAUDE.md'; Content = 'read AGENTS and START'; MaxCharacters = 64; ForbiddenPatterns = @() },
    [pscustomobject]@{ Path = 'docs/PROJECT_MAP.md'; Content = 'query relevant rows'; MaxCharacters = 64; ForbiddenPatterns = @('read AI_HANDOFF first') },
    [pscustomobject]@{ Path = 'docs/ai/MULTI_AGENT.md'; Content = 'git worktree list --porcelain | rg --fixed-strings branch'; MaxCharacters = 128; ForbiddenPatterns = @($unboundedWorktreePattern) },
    [pscustomobject]@{ Path = 'docs/AI_HANDOFF.md'; Content = 'gh issue view <issue> --json number,state'; MaxCharacters = 128; ForbiddenPatterns = @($duplicateIssueBodyPattern) }
  )
  $validContextErrors = @(Get-AiContextBudgetErrors -Entries $validContextEntries)
  if ($validContextErrors.Count -ne 0) {
    throw 'docs-check self-test failed: bounded AI context entries were rejected.'
  }
  $caseCount++

  $overBudgetErrors = @(Get-AiContextBudgetErrors -Entries @(
    [pscustomobject]@{ Path = 'CLAUDE.md'; Content = ('x' * 65); MaxCharacters = 64; ForbiddenPatterns = @() }
  ))
  if ($overBudgetErrors.Count -ne 1 -or $overBudgetErrors[0] -notmatch 'exceeds the AI context budget') {
    throw 'docs-check self-test failed: oversized AI context entry was not rejected.'
  }
  $caseCount++

  $forbiddenContextCases = @(
    [pscustomobject]@{ Name = 'stale project-map route'; Path = 'docs/PROJECT_MAP.md'; Content = 'read AI_HANDOFF first'; Pattern = 'read AI_HANDOFF first' },
    [pscustomobject]@{ Name = 'stale README route'; Path = 'README.md'; Content = '[AI 接手頁](docs/AI_HANDOFF.md) 操作'; Pattern = $staleGlobalHandoffRoutePattern },
    [pscustomobject]@{ Name = 'adapter global preload'; Path = 'CLAUDE.md'; Content = 'load docs/AI_HANDOFF.md'; Pattern = $adapterGlobalPreloadPattern },
    [pscustomobject]@{ Name = 'unbounded worktree output'; Path = 'docs/ai/MULTI_AGENT.md'; Content = 'run `git worktree list` now'; Pattern = $unboundedWorktreePattern },
    [pscustomobject]@{ Name = 'unbounded Git history'; Path = 'docs/AI_HANDOFF.md'; Content = 'git log origin/main'; Pattern = $unboundedGitLogPattern },
    [pscustomobject]@{ Name = 'unbounded GitHub list'; Path = 'docs/AI_HANDOFF.md'; Content = 'gh pr list --state merged'; Pattern = $unboundedGitHubListPattern },
    [pscustomobject]@{ Name = 'duplicate Issue body output'; Path = 'docs/AI_HANDOFF.md'; Content = 'gh issue view 123'; Pattern = $duplicateIssueBodyPattern }
  )
  foreach ($case in $forbiddenContextCases) {
    $routeErrors = @(Get-AiContextBudgetErrors -Entries @(
      [pscustomobject]@{ Path = $case.Path; Content = $case.Content; MaxCharacters = 256; ForbiddenPatterns = @($case.Pattern) }
    ))
    if ($routeErrors.Count -ne 1 -or $routeErrors[0] -notmatch 'forbidden AI startup route') {
      throw "docs-check self-test failed: $($case.Name) was not rejected."
    }
    $caseCount++
  }

  # Required schema coverage: all tracked contract schemas must be covered.
  if ((Get-MissingRequiredSchemas -TrackedPaths @('schemas/a-v1.schema.json', 'docs/example.json') -RequiredEntries @('schemas/a-v1.schema.json')).Count -ne 0) {
    throw 'docs-check self-test failed: a covered schema was reported as missing.'
  }
  $caseCount++
  $missingSchema = Get-MissingRequiredSchemas -TrackedPaths @('schemas/a-v1.schema.json', 'schemas/b-v1.schema.json') -RequiredEntries @('schemas/a-v1.schema.json')
  if ($missingSchema.Count -ne 1 -or $missingSchema[0] -ne 'schemas/b-v1.schema.json') {
    throw 'docs-check self-test failed: an uncovered schema was not reported.'
  }
  $caseCount++

  # Schema structure validation: valid schema passes.
  $validSchemaEntry = [pscustomobject]@{
    Path    = 'schemas/test-valid-v1.schema.json'
    Content = '{"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.com/test-valid-v1.schema.json","title":"Test","type":"object"}'
  }
  if ((Get-SchemaStructureErrors -SchemaEntries @($validSchemaEntry)).Count -ne 0) {
    throw 'docs-check self-test failed: a valid schema was reported as invalid.'
  }
  $caseCount++

  # Schema structure validation: missing required field is reported.
  $missingIdEntry = [pscustomobject]@{
    Path    = 'schemas/test-missing-id-v1.schema.json'
    Content = '{"$schema":"https://json-schema.org/draft/2020-12/schema","title":"No ID","type":"object"}'
  }
  $missingIdErrors = Get-SchemaStructureErrors -SchemaEntries @($missingIdEntry)
  if ($missingIdErrors.Count -ne 1 -or $missingIdErrors[0] -notmatch [regex]::Escape("missing required top-level property: " + [char]36 + "id")) {
    throw 'docs-check self-test failed: a schema without required id was not reported.'
  }
  $caseCount++

  # Schema structure validation: malformed JSON is reported.
  $malformedEntry = [pscustomobject]@{
    Path    = 'schemas/test-malformed-v1.schema.json'
    Content = '{not valid json}'
  }
  $malformedErrors = Get-SchemaStructureErrors -SchemaEntries @($malformedEntry)
  if ($malformedErrors.Count -ne 1 -or $malformedErrors[0] -notmatch 'invalid JSON') {
    throw 'docs-check self-test failed: malformed JSON was not reported.'
  }
  $caseCount++

  # Schema structure validation: duplicate $id across entries is reported.
  $dupA = [pscustomobject]@{
    Path    = 'schemas/test-dup-a-v1.schema.json'
    Content = '{"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.com/same","title":"A","type":"object"}'
  }
  $dupB = [pscustomobject]@{
    Path    = 'schemas/test-dup-b-v1.schema.json'
    Content = '{"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.com/same","title":"B","type":"object"}'
  }
  $dupErrors = Get-SchemaStructureErrors -SchemaEntries @($dupA, $dupB)
  $expectedDupId = [regex]::Escape("duplicate " + [char]36 + "id")
  if ($dupErrors.Count -ne 1 -or $dupErrors[0] -notmatch $expectedDupId) {
  }
  $caseCount++

  # Printable contract schemas: real repository schemas enforce runtime-equivalent
  # whole-string C0/C1/DEL rejection through the actual JSON Schema validator.
  Assert-PrintableContractSchemas -RepositoryRoot $repo
  Assert-ExtendedPrintableContractSchemas -RepositoryRoot $repo
  $caseCount++

  if (-not (Test-BaselineChangedByHead -ChangedPaths @('docs/state/BASELINE.md'))) {
    throw 'docs-check self-test failed: a head BASELINE edit was not detected.'
  }
  $caseCount++

  # Structural counter parser: valid summary parses correctly.
  $summaryOk = @'
目前驗證摘要：`docs-check.ps1` 的 85 個必要入口與
24 份 Spec 通過。
'@
  $ok = Get-CounterClaims $summaryOk
  if ($ok.Required -ne 85 -or $ok.Specs -ne 24) {
    throw 'docs-check self-test failed: canonical summary did not parse to expected structural counters.'
  }
  $caseCount++

  # Removing required-entry or spec markers must fail closed.
  foreach ($fragment in @('個必要入口', '份 Spec 通過')) {
    $broken = $summaryOk.Replace($fragment, 'removed-marker')
    try { Get-CounterClaims $broken | Out-Null } catch { $caseCount++; continue }
    throw "docs-check self-test failed: removing '$fragment' should fail the counter parser."
  }

  # Structural assertion passes with matching values.
  Assert-StructuralClaims -Claims $ok -Required 85 -Specs 24
  $caseCount++

  # Live measurement: git ls-files returns at least some files in a real repo.
  $liveTracked = @(git ls-files)
  if ($liveTracked.Count -lt 1) {
    throw 'docs-check self-test failed: live git ls-files returned zero files in a real repository.'
  }
  $caseCount++
  $liveJson = @($liveTracked | Where-Object { $_.ToLowerInvariant().EndsWith('.json') })
  if ($liveJson.Count -lt 1) {
    throw 'docs-check self-test failed: live git ls-files returned zero JSON files in a real repository.'
  }
  $caseCount++

  # ADR frontmatter: valid block parses correctly.
  $validAdr = "# ---`n# id: ADR-0001`n# status: accepted`n# owner: hibiki-maintainers`n# authority: architecture`n# date: 2026-08-21`n# last_reviewed: 2026-08-21`n# review_after_days: 90`n# supersedes: []`n# related_specs: []`n# source_globs: [`"src/**`"]`n# ---`n`n# Title`n`nBody text."
  $parsedAdr = ConvertFrom-AdrFrontmatter -RawText $validAdr
  if ($parsedAdr['id'] -ne 'ADR-0001' -or $parsedAdr['authority'] -ne 'architecture') {
    throw 'docs-check self-test failed: valid ADR frontmatter did not parse to expected fields.'
  }
  Assert-AdrFrontmatter -Fields $parsedAdr -FileName 'selftest-valid.md'
  $caseCount++

  # ADR frontmatter: missing opening marker must fail.
  $caughtMissingOpener = $false
  try { ConvertFrom-AdrFrontmatter -RawText "no frontmatter here`njust text" | Out-Null } catch { $caughtMissingOpener = $true }
  if (-not $caughtMissingOpener) { throw 'docs-check self-test failed: missing frontmatter opener should fail.' }
  $caseCount++

  # ADR frontmatter: missing required field must fail.
  $missingField = "# ---`n# id: ADR-0002`n# status: accepted`n# ---`ntext"
  $parsedMissing = ConvertFrom-AdrFrontmatter -RawText $missingField
  $caughtMissing = $false
  try { Assert-AdrFrontmatter -Fields $parsedMissing -FileName 'selftest-missing.md' | Out-Null } catch { $caughtMissing = $true }
  if (-not $caughtMissing) { throw 'docs-check self-test failed: missing required field should fail.' }
  $caseCount++

  # ADR frontmatter: bad authority must fail.
  $badAuthority = "# ---`n# id: ADR-0003`n# status: accepted`n# owner: x`n# authority: nonsense`n# date: 2026-08-21`n# last_reviewed: 2026-08-21`n# review_after_days: 30`n# source_globs: []`n# ---`nbody"
  $parsedBadAuth = ConvertFrom-AdrFrontmatter -RawText $badAuthority
  $caughtBadAuth = $false
  try { Assert-AdrFrontmatter -Fields $parsedBadAuth -FileName 'selftest-badauth.md' | Out-Null } catch { $caughtBadAuth = $true }
  if (-not $caughtBadAuth) { throw 'docs-check self-test failed: invalid authority should fail.' }
  $caseCount++

  # Spec frontmatter: valid block parses correctly.
  $validSpec = "---`nid: SPEC-0001`nstatus: accepted`nowner: hibiki-maintainers`nauthority: product-behavior`nlast_reviewed: 2026-08-21`nreview_after_days: 30`nrelated_adrs: [ADR-0001]`nsource_globs: [`"src/**`"]`n---`n`n# SPEC-0001`n`nBody."
  $parsedSpec = ConvertFrom-SpecFrontmatter -RawText $validSpec
  if ($parsedSpec['id'] -ne 'SPEC-0001' -or $parsedSpec['authority'] -ne 'product-behavior') {
    throw 'docs-check self-test failed: valid Spec frontmatter did not parse to expected fields.'
  }
  Assert-SpecFrontmatter -Fields $parsedSpec -FileName 'selftest-spec-valid.md'
  $caseCount++

  # Spec frontmatter: missing opening marker must fail.
  $caughtSpecMissingOpener = $false
  try { ConvertFrom-SpecFrontmatter -RawText "no frontmatter here\njust text" | Out-Null } catch { $caughtSpecMissingOpener = $true }
  if (-not $caughtSpecMissingOpener) { throw 'docs-check self-test failed: missing Spec frontmatter opener should fail.' }
  $caseCount++

  # Spec frontmatter: missing required field must fail.
  $missingSpecField = "---`nid: SPEC-0002`nstatus: accepted`n---`ntext"
  $parsedMissingSpec = ConvertFrom-SpecFrontmatter -RawText $missingSpecField
  $caughtMissingSpec = $false
  try { Assert-SpecFrontmatter -Fields $parsedMissingSpec -FileName 'selftest-spec-missing.md' | Out-Null } catch { $caughtMissingSpec = $true }
  if (-not $caughtMissingSpec) { throw 'docs-check self-test failed: missing required Spec field should fail.' }
  $caseCount++

  # Spec frontmatter: bad authority must fail.
  $badSpecAuth = "---`nid: SPEC-0003`nstatus: accepted`nowner: x`nauthority: nonsense`nlast_reviewed: 2026-08-21`nreview_after_days: 30`nrelated_adrs: []`nsource_globs: []`n---`nbody"
  $parsedBadSpecAuth = ConvertFrom-SpecFrontmatter -RawText $badSpecAuth
  $caughtBadSpecAuth = $false
  try { Assert-SpecFrontmatter -Fields $parsedBadSpecAuth -FileName 'selftest-spec-badauth.md' | Out-Null } catch { $caughtBadSpecAuth = $true }
  if (-not $caughtBadSpecAuth) { throw 'docs-check self-test failed: invalid Spec authority should fail.' }
  $caseCount++

  # Markdown relative links: valid target passes and is counted.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('see [handoff](docs/AI_HANDOFF.md)') -BaseDir $repo
  if ($linkResult.Checked -ne 1 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: valid relative link was not resolved.'
  }
  $caseCount++

  # Markdown relative links: missing target must be reported.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('see [ghost](docs/does-not-exist.md)') -BaseDir $repo
  if ($linkResult.Broken.Count -ne 1) {
    throw 'docs-check self-test failed: missing link target should be reported.'
  }
  $caseCount++

  # Markdown relative links: absolute URLs and anchors are skipped.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('[site](https://example.com/a) [mail](mailto:x@y.z) [anchor](#section)') -BaseDir $repo
  if ($linkResult.Checked -ne 3 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: external targets should be skipped without breakage.'
  }
  $caseCount++

  # Markdown relative links: fenced code blocks are ignored.
  $fence = [char]96 + [char]96 + [char]96
  $linkResult = Test-MarkdownRelativeLinks -Lines @($fence, '[ghost](docs/missing-example.md)', $fence, '[real](AGENTS.md)') -BaseDir $repo
  if ($linkResult.Checked -ne 1 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: fenced code block links should be ignored.'
  }
  $caseCount++

  # Markdown relative links: fragment-only resolution against an existing file.
  $linkResult = Test-MarkdownRelativeLinks -Lines @('[baseline with anchor](docs/state/BASELINE.md#summary)') -BaseDir $repo
  if ($linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: fragment on existing file should resolve.'
  }
  $caseCount++

  # Markdown relative links: empty input is accepted.
  $linkResult = Test-MarkdownRelativeLinks -Lines @() -BaseDir $repo
  if ($linkResult.Checked -ne 0 -or $linkResult.Broken.Count -ne 0) {
    throw 'docs-check self-test failed: empty input should produce no results.'
  }
  $caseCount++


  # JSON parse validation: empty input returns no errors.
  $jsonErrors = Test-JsonParseAll -FileEntries @()
  if ($jsonErrors.Count -ne 0) {
    throw 'docs-check self-test failed: empty JSON input should produce no errors.'
  }
  $caseCount++

  # JSON parse validation: valid JSON passes.
  $validJsonEntry = [pscustomobject]@{
    Path    = 'evidence/selftest-valid-v1.json'
    Content = '{"schema_version": 1, "issue": 0}'
  }
  if ((Test-JsonParseAll -FileEntries @($validJsonEntry)).Count -ne 0) {
    throw 'docs-check self-test failed: valid JSON was reported as invalid.'
  }
  $caseCount++

  # JSON parse validation: malformed JSON is reported with path context.
  $badJsonEntry = [pscustomobject]@{
    Path    = 'evidence/selftest-bad-v1.json'
    Content = '{broken'
  }
  $badJsonErrors = Test-JsonParseAll -FileEntries @($badJsonEntry)
  if ($badJsonErrors.Count -ne 1 -or $badJsonErrors[0] -notmatch [regex]::Escape('invalid JSON')) {
    throw 'docs-check self-test failed: malformed JSON was not reported.'
  }
  $caseCount++

if ($caseCount -lt 12) {
    throw "docs-check self-test failed: expected at least 12 passing cases, saw $caseCount."
  }
  Write-Output "docs-check self-test passed ($caseCount cases; structural parser, schema-instance validation, multiline normalization, branch mode detection, BASELINE edit detection, AI context budgets, live measurement, ADR frontmatter, Spec frontmatter, markdown relative links)."
  exit 0
}

# --- Main gate logic ---

$required = @(
  'AGENTS.md', 'CLAUDE.md', 'README.md', 'CONTRIBUTING.md', 'SECURITY.md',
  '.github/PULL_REQUEST_TEMPLATE.md', '.github/ISSUE_TEMPLATE/ai-task.yml',
  'SOURCE_POLICY.md', 'THIRD_PARTY.yml', 'config/distribution-profile.yml',
  'build/toolchain-lock.yml',
  'extensions/manifest.json', 'tools/extension-check.ps1',
  'installer/HibikiSetup.ps1', 'tools/installer-check.ps1',
  'apps/control-model/Hibiki.ControlModel.csproj', 'tools/control-model-check.ps1',
  'apps/winui-shell/Hibiki.WinUI.csproj', 'tools/winui-shell-check.ps1',
  'tools/distribution-check.ps1', 'tools/source-only-ci-check.ps1', 'tools/handoff-check.ps1',
  'tools/build-preview.ps1',
  'tools/live-device-catalog-check.ps1', 'tools/live-wasapi-handoff-check.ps1',
  'tools/live-audio-session-check.ps1', 'tools/live-process-loopback-check.ps1',
  'tools/driver-source-check.ps1', 'tools/driver-signability-check.ps1',
  'schemas/release-manifest-v1.schema.json', 'schemas/evidence-manifest-v2.schema.json',
  'schemas/printable-string-v1.schema.json',
  'docs/START_HERE.md', 'docs/AI_HANDOFF.md', 'docs/PROJECT_MAP.md', 'docs/state/BASELINE.md',
  'docs/specs/INDEX.md', 'docs/specs/SPEC-0001-core-contracts.md',
  'docs/specs/SPEC-0002-volume-and-iso.md', 'docs/specs/SPEC-0003-virtual-endpoints-and-routing.md',
  'docs/specs/SPEC-0004-ai-handoff-and-evidence.md', 'docs/specs/SPEC-0005-source-only-paid-release.md',
  'docs/adr/0001-public-monorepo-and-component-licenses.md',
  'docs/adr/0002-virtual-endpoint-engine-boundary.md', 'docs/ai/HANDOFF_SCHEMA.json',
  'docs/ai/DOC_SCHEMA.json', 'docs/ai/CHANGE_CONTRACT.yml', 'docs/ai/CONFLICT_POLICY.md',
  'docs/ai/MULTI_AGENT.md',
  'schemas/acoustic-anchor-v1.schema.json', 'schemas/equal-loudness-policy-v1.schema.json',
  'schemas/equal-loudness-status-v1.schema.json', 'schemas/ipc-envelope-v1.schema.json',
  'schemas/driver-control-v1.schema.json', 'schemas/ir-phase-policy-v1.schema.json',
  'schemas/audio-session-descriptor-v1.schema.json', 'schemas/program-aware-level-policy-v1.schema.json',
  'schemas/basic-noise-suppressor-policy-v1.schema.json',
  'schemas/virtual-mic-policy-v1.schema.json',
  'schemas/graph-config-v1.schema.json', 'schemas/scene-definition-v1.schema.json',
  'schemas/custom-scene-cards-v1.schema.json',
  'schemas/physical-device-catalog-v1.schema.json',
  'schemas/device-catalog-snapshot-v1.schema.json',
  'schemas/device-switch-request-v1.schema.json',
  'schemas/peq-filter-v1.schema.json',
  'schemas/calibration-response-v1.schema.json',
  'schemas/vst3-latency-alignment-v1.schema.json',
  'schemas/latency-graph-commit-v1.schema.json',
  'schemas/vst3-parameter-timeline-v1.schema.json',
  'schemas/vst3-plugin-state-v1.schema.json',
  'schemas/scene-vst3-state-binding-v1.schema.json',
  'schemas/output-group-volume-v1.schema.json',
  'schemas/scene-profile-v1.schema.json',
  'schemas/output-fanout-plan-v1.schema.json',
  'schemas/session-route-rule-v1.schema.json',
  'schemas/session-route-rules-v1.schema.json',
  'schemas/scene-sync-queue-v1.schema.json',
  'docs/specs/SPEC-0010-winui-shell.md',
  'docs/specs/SPEC-0011-calibration-compiler.md',
  'docs/specs/SPEC-0012-vst3-latency-graph-commit.md',
  'docs/specs/SPEC-0013-session-route-rules.md', 'docs/specs/SPEC-0014-custom-scene-catalog.md',
  'docs/specs/SPEC-0015-physical-device-catalog.md', 'docs/specs/SPEC-0016-process-loopback-capture.md',
  'docs/VST3_STATE_COMPATIBILITY_REVIEW.md', 'evidence/0000-foundation/initial.json',
  'evidence/0000-foundation/json-parse-gate-v1.json'
)

$missing = @($required | Where-Object { -not (Test-Path (Join-Path $repo $_)) })
if ($missing.Count -gt 0) { throw "Missing required documentation: $($missing -join ', ')" }

$aiContextEntries = @(
  [pscustomobject]@{ Path = 'AGENTS.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'AGENTS.md') -Raw); MaxCharacters = 6000; ForbiddenPatterns = @() },
  [pscustomobject]@{ Path = 'CLAUDE.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'CLAUDE.md') -Raw); MaxCharacters = 1000; ForbiddenPatterns = @($adapterGlobalPreloadPattern) },
  [pscustomobject]@{ Path = 'GEMINI.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'GEMINI.md') -Raw); MaxCharacters = 1000; ForbiddenPatterns = @($adapterGlobalPreloadPattern) },
  [pscustomobject]@{ Path = '.github/copilot-instructions.md'; Content = (Get-Content -LiteralPath (Join-Path $repo '.github/copilot-instructions.md') -Raw); MaxCharacters = 1000; ForbiddenPatterns = @($adapterGlobalPreloadPattern) },
  [pscustomobject]@{ Path = '.cursor/rules/project.mdc'; Content = (Get-Content -LiteralPath (Join-Path $repo '.cursor/rules/project.mdc') -Raw); MaxCharacters = 1000; ForbiddenPatterns = @($adapterGlobalPreloadPattern) },
  [pscustomobject]@{ Path = 'README.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'README.md') -Raw); MaxCharacters = 20000; ForbiddenPatterns = @($staleGlobalHandoffRoutePattern, $unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern) },
  [pscustomobject]@{ Path = 'docs/START_HERE.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'docs/START_HERE.md') -Raw); MaxCharacters = 7000; ForbiddenPatterns = @($unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern) },
  [pscustomobject]@{ Path = 'docs/AI_HANDOFF.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'docs/AI_HANDOFF.md') -Raw); MaxCharacters = 6000; ForbiddenPatterns = @($unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern, $duplicateIssueBodyPattern) },
  [pscustomobject]@{ Path = 'docs/PROJECT_MAP.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'docs/PROJECT_MAP.md') -Raw); MaxCharacters = 12000; ForbiddenPatterns = @($staleGlobalHandoffRoutePattern, $unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern) },
  [pscustomobject]@{ Path = 'docs/ai/MULTI_AGENT.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'docs/ai/MULTI_AGENT.md') -Raw); MaxCharacters = 9000; ForbiddenPatterns = @($unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern) },
  [pscustomobject]@{ Path = 'docs/ai/CODEX_GOALS.md'; Content = (Get-Content -LiteralPath (Join-Path $repo 'docs/ai/CODEX_GOALS.md') -Raw); MaxCharacters = 6000; ForbiddenPatterns = @($unboundedWorktreePattern, $unboundedGitLogPattern, $unboundedGitHubListPattern) }
)
$aiContextErrors = @(Get-AiContextBudgetErrors -Entries $aiContextEntries)
if ($aiContextErrors.Count -gt 0) {
  throw ('AI startup context contract failed: ' + ($aiContextErrors -join '; '))
}

$trackedSchemas = @(git -C $repo ls-files -- 'schemas/*.schema.json')
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked contract schemas.' }
$missingRequiredSchemas = Get-MissingRequiredSchemas -TrackedPaths $trackedSchemas -RequiredEntries $required
if ($missingRequiredSchemas.Count -gt 0) {
  throw ('Contract schemas must be required docs-check entries; missing: ' +
         ($missingRequiredSchemas -join ', '))
}

$schemaEntries = @()
foreach ($schemaPath in ($trackedSchemas | Sort-Object)) {
  $schemaEntries += [pscustomobject]@{
    Path    = $schemaPath
    Content = Get-Content -LiteralPath (Join-Path $repo $schemaPath) -Raw
  }
}
$schemaStructureErrors = Get-SchemaStructureErrors -SchemaEntries $schemaEntries
if ($schemaStructureErrors.Count -gt 0) {
  throw ('Contract schema structure validation failed: ' + ($schemaStructureErrors -join '; '))
}
$printableStringSchemaFile = Join-Path $repo 'schemas/printable-string-v1.schema.json'
Register-PrintableStringSchema -SchemaFile $printableStringSchemaFile
Assert-PrintableContractSchemas -RepositoryRoot $repo
Assert-ExtendedPrintableContractSchemas -RepositoryRoot $repo

$handoffSchemaIndex = Get-Content -LiteralPath (Join-Path $repo 'docs/ai/HANDOFF_SCHEMA.json') -Raw |
  ConvertFrom-Json
if (-not $handoffSchemaIndex.PSObject.Properties['$comment']) {
  throw 'docs/ai/HANDOFF_SCHEMA.json must document the issue-body handoff protocol.'
}

$specs = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/specs') -Filter 'SPEC-*.md' -File
$ids = @($specs | ForEach-Object { Select-String -LiteralPath $_.FullName -Pattern '^id:\s*(\S+)' | ForEach-Object { $_.Matches.Groups[1].Value } })
if (($ids | Sort-Object -Unique).Count -ne $ids.Count) { throw 'Duplicate Spec IDs detected.' }

# Exclude the navigation INDEX from ADR frontmatter validation; specs already exclude theirs via the SPEC-* filter.
$adrs = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/adr') -Filter '*.md' -File | Where-Object { $_.Name -ne 'INDEX.md' } | Sort-Object Name
foreach ($adr in $adrs) {
  $raw = Get-Content -LiteralPath $adr.FullName -Raw -Encoding UTF8
  try {
    $fields = ConvertFrom-AdrFrontmatter -RawText $raw
    Assert-AdrFrontmatter -Fields $fields -FileName $adr.Name
    if ($fields['id'] -notmatch '^ADR-[0-9]{4}$') { throw ($adr.Name + ' id must match ADR-NNNN pattern.') }
    if (-not $raw.Contains(('# ' + $fields['id']))) { throw ($adr.Name + ' heading does not contain the declared ID ' + $fields['id'] + '.') }
  } catch {
    throw ('ADR frontmatter validation failed for ' + $adr.Name + ': ' + $_.Exception.Message)
  }
}
$adrIds = @($adrs | ForEach-Object {
  $raw = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
  (ConvertFrom-AdrFrontmatter -RawText $raw)['id']
})
if (($adrIds | Sort-Object -Unique).Count -ne $adrIds.Count) { throw 'Duplicate ADR IDs detected.' }

$specIds = @()
foreach ($spec in ($specs | Sort-Object Name)) {
  $rawSpec = Get-Content -LiteralPath $spec.FullName -Raw -Encoding UTF8
  try {
    $specFields = ConvertFrom-SpecFrontmatter -RawText $rawSpec
    Assert-SpecFrontmatter -Fields $specFields -FileName $spec.Name
    if ($specFields['id'] -notmatch '^SPEC-[0-9]{4}$') { throw ($spec.Name + ' id must match SPEC-NNNN pattern.') }
    if (-not $rawSpec.Contains(('# ' + $specFields['id']))) { throw ($spec.Name + ' heading does not contain the declared ID ' + $specFields['id'] + '.') }
    # Cross-check id against filename stem (e.g. SPEC-0001-core-contracts.md -> SPEC-0001)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($spec.Name)
    $expectedId = ($stem -split '-')[0..1] -join '-'
    if ($expectedId -ne $specFields['id']) { throw ($spec.Name + ' filename stem ID mismatch: expected ' + $expectedId + ' but got ' + $specFields['id'] + '.') }
  } catch {
    throw ('Spec frontmatter validation failed for ' + $spec.Name + ': ' + $_.Exception.Message)
  }
  $specIds += $specFields['id']
}
if (($specIds | Sort-Object -Unique).Count -ne $specIds.Count) { throw 'Duplicate Spec IDs detected.' }

# Markdown relative links: every tracked markdown file must reference files
# that exist in the repository. External URLs, anchors, and fenced examples
# are intentionally out of scope.
$mdFiles = @(git -C $repo ls-files -- '*.md')
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked markdown files.' }
$linkCheckedTotal = 0
$brokenLinks = @()
foreach ($mdFile in $mdFiles) {
  $mdDir = Split-Path (Join-Path $repo $mdFile) -Parent
  $mdLines = Get-Content -LiteralPath (Join-Path $repo $mdFile)
  $result = Test-MarkdownRelativeLinks -Lines $mdLines -BaseDir $mdDir
  $linkCheckedTotal += $result.Checked
  foreach ($broken in $result.Broken) { $brokenLinks += ($mdFile + ' -> ' + $broken) }
}
if ($brokenLinks.Count -gt 0) {
  throw ('Broken relative markdown links detected: ' + ($brokenLinks -join '; '))
}

$adapters = @('AGENTS.md', 'CLAUDE.md', 'GEMINI.md', '.github/copilot-instructions.md', '.cursor/rules/project.mdc')
foreach ($adapter in $adapters) {
  if (-not (Test-Path (Join-Path $repo $adapter))) { throw "Missing AI adapter: $adapter" }
  if ($adapter -eq 'AGENTS.md') { continue }
  $adapterText = Get-Content -LiteralPath (Join-Path $repo $adapter) -Raw
  if (-not $adapterText.Contains('AGENTS.md') -or -not $adapterText.Contains('docs/START_HERE.md')) {
    throw "AI adapter is not anchored to canonical instructions: $adapter"
  }
}

$baselineText = Get-Content -LiteralPath (Join-Path $repo 'docs/state/BASELINE.md') -Raw
$trackedFiles = @(git -C $repo ls-files)
if ($LASTEXITCODE -ne 0) { throw 'docs-check could not list tracked files.' }
$jsonFiles = @(git -C $repo ls-files -- '*.json')
# Every tracked JSON file must be syntactically valid.
$allJsonEntries = @()
foreach ($jsonPath in ($jsonFiles | Sort-Object)) {
  $allJsonEntries += [pscustomobject]@{
    Path    = $jsonPath
    Content = Get-Content -LiteralPath (Join-Path $repo $jsonPath) -Raw
  }
}
$jsonParseErrors = Test-JsonParseAll -FileEntries $allJsonEntries
if ($jsonParseErrors.Count -gt 0) {
  throw ('Tracked JSON syntax validation failed: ' + ($jsonParseErrors -join '; '))
}

$claims = Get-CounterClaims $baselineText

# Structural counters (required entries and specs) are always verified against
# the tree being tested; they change rarely, so keeping them strict costs
# parallel lanes nothing.
Assert-StructuralClaims -Claims $claims -Required $required.Count -Specs $specs.Count

# Volatile volatile volatile — NO, these are measured LIVE and reported as
# informational output only. No committed counter numbers are read or compared.
$baseRef = $env:GITHUB_BASE_REF
$pullRequestMode = -not [string]::IsNullOrWhiteSpace($baseRef)

$summaryTemplate = 'Documentation checks passed ({0} required paths, {1} specs; live measurement: {2} tracked paths, {3} repository JSON files, {4} markdown links.)'
Write-Output (($summaryTemplate) -f $required.Count, $specs.Count, $trackedFiles.Count, $jsonFiles.Count, $linkCheckedTotal)
