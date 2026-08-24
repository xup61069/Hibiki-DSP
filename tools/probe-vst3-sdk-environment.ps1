#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest,
  [string]$SdkRoot
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$local = Join-Path $repo '.local'

# Required relative paths mirror vst-host/CMakeLists.txt. This probe is read-only
# and records only anonymous presence facts under .local/; it never copies or
# commits SDK source, binaries, private paths, or plugin content.
$requiredPaths = @(
  'pluginterfaces/vst/ivstcomponent.h',
  'public.sdk/source/vst/hosting/module.cpp',
  'public.sdk/source/vst/hosting/module_win32.cpp',
  'public.sdk/source/vst/hosting/hostclasses.cpp',
  'public.sdk/source/vst/hosting/parameterchanges.cpp',
  'public.sdk/source/vst/hosting/pluginterfacesupport.cpp',
  'public.sdk/source/vst/utility/stringconvert.cpp',
  'public.sdk/source/common/commonstringconvert.cpp',
  'public.sdk/source/vst/vstinitiids.cpp',
  'pluginterfaces/base/coreiids.cpp',
  'pluginterfaces/base/conststringtable.cpp',
  'pluginterfaces/base/funknown.cpp'
)

function Test-Vst3RequiredPath {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$RelativePath
  )

  $candidate = Join-Path -Path $Root -ChildPath ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
  return Test-Path -LiteralPath $candidate -PathType Leaf
}

function Get-Vst3ProbeResult {
  param(
    [AllowNull()][string]$ConfiguredRoot
  )

  $exists = -not [string]::IsNullOrWhiteSpace($ConfiguredRoot) -and
    (Test-Path -LiteralPath $ConfiguredRoot -PathType Container)

  $files = [ordered]@{}
  foreach ($relativePath in $requiredPaths) {
    if ($exists) {
      $files[$relativePath] = Test-Vst3RequiredPath -Root $ConfiguredRoot -RelativePath $relativePath
    } else {
      $files[$relativePath] = $false
    }
  }

  return [ordered]@{
    schema_version = 1
    scope_digest = 'vst3-sdk-environment-v1'
    exists = $exists
    files = $files
  }
}

function Assert-Vst3AnonymousResult {
  param(
    [Parameter(Mandatory = $true)]$Result
  )

  $serialized = $Result | ConvertTo-Json -Depth 8
  if ($serialized -match '(?i)(private_path|calibration_path|user_profile|endpoint_id|serial_number|device_id|friendly_name|sdk_root)') {
    throw 'VST3 environment result contains a forbidden identity-like key.'
  }
  foreach ($property in $Result.PSObject.Properties) {
    if ($property.Name -match '(?i)(path|root)' -and $property.Value -is [string] -and $property.Value.Contains(':')) {
      throw 'VST3 anonymous result must not include a filesystem path.'
    }
  }
}

if ($SelfTest) {
  $absent = Get-Vst3ProbeResult -ConfiguredRoot $null
  Assert-Vst3AnonymousResult -Result $absent
  if ($absent.exists) {
    throw 'Absent SDK self-test unexpectedly reported exists=true.'
  }
  foreach ($present in $absent.files.Values) {
    if ($present) {
      throw 'Absent SDK self-test unexpectedly reported a required file as present.'
    }
  }

  $fakeRoot = Join-Path ([IO.Path]::GetTempPath()) ('hibiki-vst3-probe-' + [Guid]::NewGuid().ToString('N'))
  try {
    New-Item -ItemType Directory -Path $fakeRoot | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'pluginterfaces/vst') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fakeRoot 'pluginterfaces/vst/ivstcomponent.h') -Value 'selftest' -Encoding UTF8NoBOM
    $partial = Get-Vst3ProbeResult -ConfiguredRoot $fakeRoot
    Assert-Vst3AnonymousResult -Result $partial
    if (-not $partial.exists) {
      throw 'Present root self-test unexpectedly reported exists=false.'
    }
    if (-not $partial.files['pluginterfaces/vst/ivstcomponent.h']) {
      throw 'Partial SDK self-test did not detect its temporary fixture file.'
    }
    if ($partial.files['pluginterfaces/base/funknown.cpp']) {
      throw 'Partial SDK self-test incorrectly reported an absent file.'
    }
  } finally {
    if (Test-Path -LiteralPath $fakeRoot) {
      Remove-Item -LiteralPath $fakeRoot -Recurse -Force
    }
  }

  'vst3-sdk-environment selftest passed'
  return
}

$resolvedRoot = $SdkRoot
if ([string]::IsNullOrWhiteSpace($resolvedRoot)) {
  $resolvedRoot = $env:HIBIKI_VST3_SDK_ROOT
}

$result = Get-Vst3ProbeResult -ConfiguredRoot $resolvedRoot
Assert-Vst3AnonymousResult -Result $result

if (-not (Test-Path -LiteralPath $local -PathType Container)) {
  New-Item -ItemType Directory -Path $local | Out-Null
}
$outputPath = Join-Path $local 'vst3-sdk-environment.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputPath -Encoding UTF8NoBOM

"vst3-sdk-environment probe passed; exists=$($result.exists); output=$outputPath"
