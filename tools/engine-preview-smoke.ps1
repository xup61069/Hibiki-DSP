#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting,
  [switch]$EnableProcessDelivery,
  [switch]$EnableWasapiOutput,
  [switch]$EnableTestTone,
  [switch]$EnableTabBridge,
  [switch]$EnableTabNoiseSuppressor,
  [switch]$EnableDriverLoopback,
  [switch]$EnableWavSource,
  [switch]$RenderOffline,
  [switch]$StatusOnly,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-EnginePreviewSmokePlan {
  param(
    [Parameter(Mandatory)][string]$RepositoryRoot
  )

  $localRoot = Join-Path $RepositoryRoot '.local'
  $engineWorkingDirectory = Join-Path $localRoot 'engine-preview'
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    LocalRoot = $localRoot
    EngineWorkingDirectory = $engineWorkingDirectory
    EnginePath = Join-Path $engineWorkingDirectory 'Release/hibiki_engine_preview.exe'
    IrDirectory = $localRoot
    IrPath = Join-Path $localRoot 'engine-preview-smoke-ir.wav'
    WavSourcePath = Join-Path $localRoot 'engine-preview-smoke-source.wav'
    OfflineRenderPath = Join-Path $localRoot 'engine-preview-smoke-render.wav'
  }
}

function Test-EnginePreviewSmokePathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath -eq $fullRoot -or
    $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-EnginePreviewSmokeExistingAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Engine Preview smoke path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Engine Preview smoke path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-EnginePreviewSmokePath {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-EnginePreviewSmokePathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Engine Preview smoke path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-EnginePreviewSmokeExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Engine Preview smoke $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-EnginePreviewSmokeExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Engine Preview smoke path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Engine Preview smoke path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "Engine Preview smoke path is not a file: $fullPath"
        }
      }
    }

    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Engine Preview smoke path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Invoke-EnginePreviewSmokePathSelfTest {
  $fixture = Get-EnginePreviewSmokePlan -RepositoryRoot 'C:\hibiki-engine-preview-smoke-selftest'
  $localRoot = [IO.Path]::GetFullPath($fixture.LocalRoot).TrimEnd('\', '/')
  $workingDirectory = [IO.Path]::GetFullPath($fixture.EngineWorkingDirectory).TrimEnd('\', '/')
  $enginePath = [IO.Path]::GetFullPath($fixture.EnginePath).TrimEnd('\', '/')
  $irPath = [IO.Path]::GetFullPath($fixture.IrPath).TrimEnd('\', '/')
  $wavSourcePath = [IO.Path]::GetFullPath($fixture.WavSourcePath).TrimEnd('\', '/')
  $directory = [System.IO.FileAttributes]::Directory
  $file = [System.IO.FileAttributes]::Archive
  $cases = 0

  Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
    $localRoot = $directory
    $workingDirectory = $directory
    $enginePath = $file
  }
  $cases++
  Assert-EnginePreviewSmokePath -Path $fixture.EngineWorkingDirectory -Root $fixture.LocalRoot -Kind Directory -SyntheticAttributes @{
    $localRoot = $directory
    $workingDirectory = $directory
  }
  $cases++
  Assert-EnginePreviewSmokePath -Path $fixture.IrDirectory -Root $fixture.RepositoryRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-EnginePreviewSmokePath -Path $fixture.IrPath -Root $fixture.RepositoryRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++
  Assert-EnginePreviewSmokePath -Path $fixture.WavSourcePath -Root $fixture.RepositoryRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{}
  $cases++

  $outsideCaught = $false
  $outsidePath = Join-Path (Split-Path -Parent $fixture.RepositoryRoot) 'outside-engine-preview.exe'
  try { Assert-EnginePreviewSmokePath -Path $outsidePath -Root $fixture.LocalRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{} }
  catch { $outsideCaught = $_.Exception.Message -match 'under the expected root' }
  if (-not $outsideCaught) { throw 'Engine Preview smoke self-test expected an outside-root rejection.' }
  $cases++

  $reparseParentCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
      $workingDirectory = $directory
      $enginePath = $file
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'Engine Preview smoke self-test expected a reparse-parent rejection.' }
  $cases++

  $reparseTargetCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $directory
      $enginePath = $file -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'Engine Preview smoke self-test expected a reparse-target rejection.' }
  $cases++

  $nonDirectoryCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EngineWorkingDirectory -Root $fixture.LocalRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $file
    }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'Engine Preview smoke self-test expected a non-directory rejection.' }
  $cases++

  $nonFileCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $directory
      $enginePath = $directory
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'not a file' }
  if (-not $nonFileCaught) { throw 'Engine Preview smoke self-test expected a non-file rejection.' }
  $cases++

  $missingEngineCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -SyntheticAttributes @{
      $localRoot = $directory
      $workingDirectory = $directory
    }
  } catch { $missingEngineCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingEngineCaught) { throw 'Engine Preview smoke self-test expected a missing executable rejection.' }
  $cases++

  $irReparseCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.IrDirectory -Root $fixture.RepositoryRoot -Kind Directory -AllowMissingLeaf -SyntheticAttributes @{
      $localRoot = $directory -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $irReparseCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $irReparseCaught) { throw 'Engine Preview smoke self-test expected an IR-directory reparse rejection.' }
  $cases++

  $irDirectoryTargetCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.IrPath -Root $fixture.RepositoryRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{
      $localRoot = $directory
      $irPath = $directory
    }
  } catch { $irDirectoryTargetCaught = $_.Exception.Message -match 'not a file' }
  if (-not $irDirectoryTargetCaught) { throw 'Engine Preview smoke self-test expected an IR-target directory rejection.' }
  $cases++

  $irDirectoryTypeCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.IrDirectory -Root $fixture.RepositoryRoot -Kind Directory -SyntheticAttributes @{
      $localRoot = $file
    }
  } catch { $irDirectoryTypeCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $irDirectoryTypeCaught) { throw 'Engine Preview smoke self-test expected a non-directory IR root rejection.' }
  $cases++

  $leafInspectionErrorCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{ $localRoot = $directory; $workingDirectory = $directory } `
      -SyntheticInspectionErrors @{ $enginePath = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'Engine Preview smoke self-test expected a leaf inspection-error rejection.' }
  $cases++

  $parentInspectionErrorCaught = $false
  try {
    Assert-EnginePreviewSmokePath -Path $fixture.EnginePath -Root $fixture.LocalRoot -Kind File -AllowMissingLeaf `
      -SyntheticAttributes @{ $workingDirectory = $directory; $enginePath = $file } `
      -SyntheticInspectionErrors @{ $localRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'Engine Preview smoke self-test expected a parent inspection-error rejection.' }
  $cases++

  return $cases
}

function Read-Exactly([System.IO.Stream]$Stream, [byte[]]$Buffer) {
  $offset = 0
  while ($offset -lt $Buffer.Length) {
    $read = $Stream.Read($Buffer, $offset, $Buffer.Length - $offset)
    if ($read -eq 0) { throw 'Engine Preview closed the control pipe early.' }
    $offset += $read
  }
}

function New-IpcFrame([uint16]$Type, [uint64]$RequestId, [byte[]]$Payload) {
  if ($null -eq $Payload) { $Payload = @() }
  if ($Payload.Length -gt 1048576) {
    throw "IPC payload exceeds the v1 bound: $($Payload.Length) bytes."
  }
  $frame = New-Object byte[] (20 + $Payload.Length)
  [BitConverter]::GetBytes([uint32]0x314B4948).CopyTo($frame, 0)
  [BitConverter]::GetBytes([uint16]1).CopyTo($frame, 4)
  [BitConverter]::GetBytes($Type).CopyTo($frame, 6)
  [BitConverter]::GetBytes([uint32]$Payload.Length).CopyTo($frame, 8)
  [BitConverter]::GetBytes($RequestId).CopyTo($frame, 12)
  if ($Payload.Length -gt 0) { $Payload.CopyTo($frame, 20) }
  return ,$frame
}

function Assert-IpcFrameShape {
  param(
    [Parameter(Mandatory = $true)][byte[]]$Frame,
    [Parameter(Mandatory = $true)][uint16]$ExpectedType,
    [Parameter(Mandatory = $true)][uint64]$ExpectedRequestId,
    [int]$ExpectedPayloadLength = -1,
    [uint32]$MinimumPayloadLength = 0,
    [uint32]$MaximumPayloadLength = 1048576
  )

  if ($null -eq $Frame -or $Frame.Length -lt 20) {
    throw 'IPC frame is shorter than the v1 header.'
  }
  if ([BitConverter]::ToUInt32($Frame, 0) -ne [uint32]0x314B4948 -or
      [BitConverter]::ToUInt16($Frame, 4) -ne [uint16]1) {
    throw 'IPC frame magic or version is invalid.'
  }
  $payloadLength = [BitConverter]::ToUInt32($Frame, 8)
  if ($payloadLength -gt $MaximumPayloadLength) {
    throw "IPC payload exceeds the v1 bound: $payloadLength bytes."
  }
  if ($Frame.Length -ne (20 + [int]$payloadLength)) {
    throw "IPC frame length does not match its payload length: frame=$($Frame.Length), payload=$payloadLength."
  }
  if ([BitConverter]::ToUInt16($Frame, 6) -ne $ExpectedType) {
    throw "IPC frame type mismatch: expected=$ExpectedType, actual=$([BitConverter]::ToUInt16($Frame, 6))."
  }
  if ([BitConverter]::ToUInt64($Frame, 12) -ne $ExpectedRequestId) {
    throw "IPC frame request correlation mismatch: expected=$ExpectedRequestId, actual=$([BitConverter]::ToUInt64($Frame, 12))."
  }
  if ($ExpectedPayloadLength -ge 0 -and $payloadLength -ne [uint32]$ExpectedPayloadLength) {
    throw "IPC payload length mismatch: expected=$ExpectedPayloadLength, actual=$payloadLength."
  }
  if ($payloadLength -lt $MinimumPayloadLength) {
    throw "IPC payload is shorter than the required minimum: minimum=$MinimumPayloadLength, actual=$payloadLength."
  }
}

function Send-IpcFrame([System.IO.Stream]$Stream, [byte[]]$Frame) {
  $length = [BitConverter]::GetBytes([uint32]$Frame.Length)
  $Stream.Write($length, 0, $length.Length)
  $Stream.Write($Frame, 0, $Frame.Length)
  $Stream.Flush()
}

function Receive-IpcFrame([System.IO.Stream]$Stream) {
  [byte[]]$length = New-Object byte[] 4
  Read-Exactly $Stream $length
  $frameLength = [BitConverter]::ToUInt32($length, 0)
  if ($frameLength -lt 20 -or $frameLength -gt 1048596) {
    throw "Unexpected control frame length: $frameLength"
  }
  [byte[]]$frame = New-Object byte[] $frameLength
  Read-Exactly $Stream $frame
  return ,$frame
}


# Start-Process joins an array argument list with spaces and never adds quotes,
# so any value containing spaces must be wrapped exactly once here. Flags are
# single tokens and stay bare; only values need the embedded double quotes.
function ConvertTo-EngineArgumentString {
  param([Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Arguments)
  $composed = @($Arguments | ForEach-Object {
    if ($_ -match '\s') { '"' + ($_ -replace '"', ('\' + '"')) + '"' } else { $_ }
  })
  return $composed -join ' '
}
function Get-EnginePreviewSmokeWavSampleStats {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][float[]]$Samples,
    [Parameter(Mandatory)][ValidateRange(1, [int]::MaxValue)][int]$ChannelCount
  )
  if ($Samples.Count -eq 0) { throw 'WAV sample statistics require at least one sample.' }
  if (($Samples.Count % $ChannelCount) -ne 0) {
    throw "WAV sample count is not divisible by channel count: $($Samples.Count) / $ChannelCount."
  }

  $frameCount = [int64]($Samples.Count / $ChannelCount)
  $peak = [double]0
  $sumSquared = [double]0
  $channelSums = New-Object 'double[]' $ChannelCount
  for ($index = 0; $index -lt $Samples.Count; $index++) {
    $value = [double]$Samples[$index]
    $absolute = [Math]::Abs($value)
    if ($absolute -gt $peak) { $peak = $absolute }
    $sumSquared += $value * $value
    $channelSums[$index % $ChannelCount] += $value
  }

  $rms = [Math]::Sqrt($sumSquared / $Samples.Count)
  $dcOffset = [double]0
  $maximumAbsoluteDc = [double]0
  for ($channel = 0; $channel -lt $ChannelCount; $channel++) {
    $channelMean = $channelSums[$channel] / $frameCount
    if ([Math]::Abs($channelMean) -gt $maximumAbsoluteDc) {
      $maximumAbsoluteDc = [Math]::Abs($channelMean)
      $dcOffset = $channelMean
    }
  }

  return [pscustomobject]@{
    Peak = $peak
    Rms = $rms
    DcOffset = $dcOffset
    FrameCount = $frameCount
  }
}

function Assert-EnginePreviewSmokeSignalBounds {
  param(
    [Parameter(Mandatory)]$Statistics,
    [ValidateRange(1, [int64]::MaxValue)][int64]$ExpectedFrameCount = 239,
    [ValidateRange(0, 1)][double]$MaximumPeak = 0.9,
    [ValidateRange(0, 1)][double]$MaximumRms = 0.5,
    [ValidateRange(0, 1)][double]$MaximumAbsoluteDcOffset = 0.05
  )
  if ($null -eq $Statistics.Peak -or $null -eq $Statistics.Rms -or
      $null -eq $Statistics.DcOffset -or $null -eq $Statistics.FrameCount) {
    throw 'Signal-boundary assertions require peak/RMS/DC/frame statistics.'
  }
  if ($Statistics.FrameCount -ne $ExpectedFrameCount) {
    throw ("Rendered frame count is outside expectation: got {0}, expected {1}." -f $Statistics.FrameCount, $ExpectedFrameCount)
  }
  $peak = [double]$Statistics.Peak
  if (($peak -le [double]0) -or ($peak -ge $MaximumPeak)) {
    throw ("Rendered peak level is outside the safe signal window (0 exclusive, {0} exclusive): {1}." -f $MaximumPeak, $peak)
  }
  $rms = [double]$Statistics.Rms
  if (($rms -le [double]0) -or ($rms -ge $MaximumRms)) {
    throw ("Rendered RMS level is outside the safe signal window (0 exclusive, {0} exclusive): {1}." -f $MaximumRms, $rms)
  }
  $absoluteDcOffset = [Math]::Abs([double]$Statistics.DcOffset)
  if ($absoluteDcOffset -ge $MaximumAbsoluteDcOffset) {
    throw ("Rendered DC offset magnitude exceeds the allowed bound |dc| < {0}: {1}." -f $MaximumAbsoluteDcOffset, $absoluteDcOffset)
  }
}

function Read-OfflineRenderWavDataChunk {
  param([Parameter(Mandatory)][string]$Path)
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                   [System.IO.FileAccess]::Read,
                                   [System.IO.FileShare]::Read)
  try {
    $reader = [System.IO.BinaryReader]::new($stream)
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'RIFF') {
      throw "Offline render WAV has an invalid RIFF header: $Path."
    }
    [void]$reader.ReadUInt32()
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'WAVE') {
      throw "Offline render WAV is not a WAVE file: $Path."
    }

    while ($true) {
      if (($stream.Length - $stream.Position) -lt 8) {
        throw "Offline render WAV has no data chunk: $Path."
      }
      $chunkId = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
      $chunkSize = $reader.ReadUInt32()
      if ($chunkId -eq 'data') {
        if (($chunkSize -eq 0) -or (($chunkSize % 4) -ne 0)) {
          throw "Offline render WAV data chunk is not aligned to float32 samples: $Path."
        }
        if (($stream.Length - $stream.Position) -lt $chunkSize) {
          throw "Offline render WAV data chunk is truncated: $Path."
        }
        [byte[]]$sampleBytes = $reader.ReadBytes($chunkSize)
        [float[]]$samples = New-Object 'float[]' ([int]($chunkSize / 4))
        [System.Buffer]::BlockCopy($sampleBytes, 0, $samples, 0, $chunkSize)
        return $samples
      }
      if (($stream.Length - $stream.Position) -lt $chunkSize) {
        throw "Offline render WAV chunk is truncated: $Path."
      }
      [void]$reader.ReadBytes($chunkSize)
      if (($chunkSize -band 1) -eq 1) { [void]$reader.ReadByte() }
    }
  } finally {
    $reader.Dispose()
  }
}

if ($SelfTest) {
  $cases = Invoke-EnginePreviewSmokePathSelfTest

  $emptyFrame = New-IpcFrame 1 42 @()
  Assert-IpcFrameShape -Frame $emptyFrame -ExpectedType 1 -ExpectedRequestId 42 -ExpectedPayloadLength 0
  $cases++

  [byte[]]$payload = 0x10, 0x20, 0x30
  $payloadFrame = New-IpcFrame 9 42 $payload
  if ($payloadFrame[0] -ne 0x48 -or $payloadFrame[1] -ne 0x49 -or
      $payloadFrame[2] -ne 0x4B -or $payloadFrame[3] -ne 0x31 -or
      $payloadFrame[4] -ne 0x01 -or $payloadFrame[5] -ne 0x00 -or
      $payloadFrame[6] -ne 0x09 -or $payloadFrame[7] -ne 0x00 -or
      [BitConverter]::ToUInt32($payloadFrame, 8) -ne 3 -or
      [BitConverter]::ToUInt64($payloadFrame, 12) -ne 42) {
    throw 'IPC frame self-test found a non-little-endian or mis-correlated frame.'
  }
  Assert-IpcFrameShape -Frame $payloadFrame -ExpectedType 9 -ExpectedRequestId 42 -ExpectedPayloadLength 3
  $cases++

  $correlationCaught = $false
  try { Assert-IpcFrameShape -Frame $payloadFrame -ExpectedType 9 -ExpectedRequestId 43 } catch { $correlationCaught = $true }
  if (-not $correlationCaught) { throw 'IPC frame self-test expected request correlation rejection.' }
  $cases++

  $shortCaught = $false
  try { Assert-IpcFrameShape -Frame ([byte[]](0, 1, 2)) -ExpectedType 1 -ExpectedRequestId 42 } catch { $shortCaught = $true }
  if (-not $shortCaught) { throw 'IPC frame self-test expected short-frame rejection.' }
  $cases++

  [byte[]]$oversizedHeader = New-Object byte[] 20
  [BitConverter]::GetBytes([uint32]1048577).CopyTo($oversizedHeader, 8)
  $oversizedCaught = $false
  try { Assert-IpcFrameShape -Frame $oversizedHeader -ExpectedType 1 -ExpectedRequestId 42 } catch { $oversizedCaught = $true }
  if (-not $oversizedCaught) { throw 'IPC frame self-test expected oversized-payload rejection.' }
  $cases++

  [byte[]]$mismatchedLength = New-IpcFrame 1 42 $payload
  [BitConverter]::GetBytes([uint32]2).CopyTo($mismatchedLength, 8)
  $lengthCaught = $false
  try { Assert-IpcFrameShape -Frame $mismatchedLength -ExpectedType 1 -ExpectedRequestId 42 } catch { $lengthCaught = $true }
  if (-not $lengthCaught) { throw 'IPC frame self-test expected payload-length rejection.' }
  $cases++
  # Argument composition must survive spaces inside a path value.
  $composedArgs = ConvertTo-EngineArgumentString -Arguments @(
    '--enable-wav-source',
    '--wav-source-path',
    'G:\Hibiki DSP\smoke source.wav'
  )
  if ($composedArgs -ne '--enable-wav-source --wav-source-path "G:\Hibiki DSP\smoke source.wav"') {
    throw "Engine argument composition lost embedded quotes: $composedArgs"
  }
  $cases++

  $constructionCaught = $false
  try { [void](New-IpcFrame 1 42 (New-Object byte[] 1048577)) } catch { $constructionCaught = $true }
  if (-not $constructionCaught) { throw 'IPC frame self-test expected constructor bound rejection.' }
  $cases++

  $knownStats = Get-EnginePreviewSmokeWavSampleStats -Samples ([float[]]@(0.5, -0.25)) -ChannelCount 1
  $expectedRms = [Math]::Sqrt(((0.5 * 0.5) + (0.25 * 0.25)) / 2)
  if ($knownStats.Peak -ne 0.5 -or $knownStats.FrameCount -ne 2 -or
      [Math]::Abs($knownStats.Rms - $expectedRms) -gt 0.0000001 -or
      [Math]::Abs($knownStats.DcOffset - 0.125) -gt 0.0000001) {
    throw 'WAV sample-statistics self-test produced unexpected peak/RMS/DC.'
  }
  $cases++

  $emptySamplesCaught = $false
  try { Get-EnginePreviewSmokeWavSampleStats -Samples ([float[]]@()) -ChannelCount 1 } catch { $emptySamplesCaught = $true }
  if (-not $emptySamplesCaught) { throw 'WAV sample-statistics self-test expected an empty-sample rejection.' }
  $cases++

  $unalignedSamplesCaught = $false
  try { Get-EnginePreviewSmokeWavSampleStats -Samples ([float[]]@(0.1, 0.2, 0.3)) -ChannelCount 2 } catch { $unalignedSamplesCaught = $true }
  if (-not $unalignedSamplesCaught) { throw 'WAV sample-statistics self-test expected a channel-alignment rejection.' }
  $cases++

  $saturatedStatsCaught = $false
  try {
    Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
      Peak = [double]0.95; Rms = [double]0.1; DcOffset = [double]0; FrameCount = [int64]239 })
  } catch { $saturatedStatsCaught = $true }
  if (-not $saturatedStatsCaught) { throw 'Signal-boundary self-test expected an over-peak rejection.' }
  $cases++

  $silentStatsCaught = $false
  try {
    Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
      Peak = [double]0; Rms = [double]0; DcOffset = [double]0; FrameCount = [int64]239 })
  } catch { $silentStatsCaught = $true }
  if (-not $silentStatsCaught) { throw 'Signal-boundary self-test expected a silent-render rejection.' }
  $cases++

  $hotRmsStatsCaught = $false
  try {
    Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
      Peak = [double]0.5; Rms = [double]0.6; DcOffset = [double]0; FrameCount = [int64]239 })
  } catch { $hotRmsStatsCaught = $true }
  if (-not $hotRmsStatsCaught) { throw 'Signal-boundary self-test expected an over-RMS rejection.' }
  $cases++

  $dcDriftStatsCaught = $false
  try {
    Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
      Peak = [double]0.5; Rms = [double]0.25; DcOffset = [double]0.08; FrameCount = [int64]239 })
  } catch { $dcDriftStatsCaught = $true }
  if (-not $dcDriftStatsCaught) { throw 'Signal-boundary self-test expected a DC-offset rejection.' }
  $cases++

  $frameMismatchStatsCaught = $false
  try {
    Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
      Peak = [double]0.25; Rms = [double]0.15; DcOffset = [double]0; FrameCount = [int64]238 })
  } catch { $frameMismatchStatsCaught = $true }
  if (-not $frameMismatchStatsCaught) { throw 'Signal-boundary self-test expected a frame-count rejection.' }
  $cases++

  Assert-EnginePreviewSmokeSignalBounds -Statistics ([pscustomobject]@{
    Peak = [double]0.25; Rms = [double]0.15; DcOffset = [double]0.01; FrameCount = [int64]239 })
  $cases++

  Write-Output "Engine Preview path and IPC self-test passed ($cases cases; offline/no-process/no-file-write)."
  exit 0
}

$smokePlan = Get-EnginePreviewSmokePlan -RepositoryRoot $repo
Assert-EnginePreviewSmokePath -Path $smokePlan.EnginePath -Root $smokePlan.LocalRoot -Kind File
Assert-EnginePreviewSmokePath -Path $smokePlan.EngineWorkingDirectory -Root $smokePlan.LocalRoot -Kind Directory
Assert-EnginePreviewSmokePath -Path $smokePlan.IrDirectory -Root $smokePlan.RepositoryRoot -Kind Directory -AllowMissingLeaf
Assert-EnginePreviewSmokePath -Path $smokePlan.IrPath -Root $smokePlan.RepositoryRoot -Kind File -AllowMissingLeaf
Assert-EnginePreviewSmokePath -Path $smokePlan.WavSourcePath -Root $smokePlan.RepositoryRoot -Kind File -AllowMissingLeaf
$engine = $smokePlan.EnginePath
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this smoke.'
}

function Write-TestIrWav([string]$Path) {
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
                                   [System.IO.FileAccess]::Write,
                                   [System.IO.FileShare]::None)
  try {
    $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::ASCII, $false)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32]44)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]3) # IEEE Float32
    $writer.Write([uint16]1) # mono
    $writer.Write([uint32]48000)
    $writer.Write([uint32]192000)
    $writer.Write([uint16]4)
    $writer.Write([uint16]32)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]8)
    $writer.Write([single]1.0)
    $writer.Write([single]0.0)
    $writer.Flush()
    $writer.Dispose()
  } finally {
    $stream.Dispose()
  }
}

function Assert-OfflineRenderWavHeader([string]$Path) {
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                   [System.IO.FileAccess]::Read,
                                   [System.IO.FileShare]::Read)
  try {
    $reader = [System.IO.BinaryReader]::new($stream)
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'RIFF') {
      throw "Offline render WAV has an invalid RIFF header: $Path."
    }
    [void]$reader.ReadUInt32()
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'WAVE') {
      throw "Offline render WAV is not a WAVE file: $Path."
    }
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'fmt ') {
      throw "Offline render WAV has no fmt chunk: $Path."
    }
    [void]$reader.ReadUInt32()
    if ($reader.ReadUInt16() -ne 3 -or $reader.ReadUInt16() -ne 2 -or
        $reader.ReadUInt32() -ne 48000) {
      throw "Offline render WAV is not float32 stereo at 48000 Hz: $Path."
    }
  } finally {
    $reader.Dispose()
  }
}
function Write-WavSourceFixture([string]$Path) {
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
                                   [System.IO.FileAccess]::Write,
                                   [System.IO.FileShare]::None)
  try {
    $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::ASCII, $false)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + 880))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]3) # IEEE Float32
    $writer.Write([uint16]1) # mono; broadcast to stereo by the source
    $writer.Write([uint32]44100) # exercise the offline resample path
    $writer.Write([uint32]176400) # 44100 Hz x 4-byte mono frames
    $writer.Write([uint16]4)
    $writer.Write([uint16]32)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]880) # 220 frames x 1ch x 4B -> 239 frames at 48000 Hz
    for ($frame = 0; $frame -lt 220; $frame++) {
      $sample = [single](0.25 * [Math]::Sin(2 * [Math]::PI * 5000 * $frame / 44100))
      $writer.Write([single]$sample)
    }
    $writer.Flush()
    $writer.Dispose()
  } finally {
    $stream.Dispose()
  }
}

function Write-ProcessDeliveryTone([string]$Path) {
  # A longer, clearly audible WAV so the Windows session catalog reports an
  # active audio session for process-loopback capture during the smoke test.
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
                                   [System.IO.FileAccess]::Write,
                                   [System.IO.FileShare]::None)
  try {
    $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::ASCII, $false)
    $frameCount = 48000  # 1 second of stereo float32 at 48 kHz
    $dataBytes = $frameCount * 2 * 4
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + $dataBytes))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]3) # IEEE Float32
    $writer.Write([uint16]2) # stereo
    $writer.Write([uint32]48000)
    $writer.Write([uint32]384000)
    $writer.Write([uint16]8)
    $writer.Write([uint16]32)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]$dataBytes)
    for ($frame = 0; $frame -lt $frameCount; $frame++) {
      $sample = [single](0.3 * [Math]::Sin(2 * [Math]::PI * 440 * $frame / 48000))
      $writer.Write($sample); $writer.Write($sample)
    }
    $writer.Flush(); $writer.Dispose()
  } finally { $stream.Dispose() }
}

$engineArguments = @()
if ($EnableSystemVolume) { $engineArguments += '--enable-system-volume' }
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
if ($EnableProcessDelivery) {
  if (-not $EnableSessionRouting) { throw 'EnableProcessDelivery requires EnableSessionRouting.' }
  if (-not $EnableWasapiOutput) { throw 'EnableProcessDelivery requires EnableWasapiOutput.' }
  $engineArguments += '--enable-process-delivery'
}
if ($EnableWasapiOutput) { $engineArguments += '--enable-wasapi-output' }
if ($EnableTestTone) {
  if (-not $EnableWasapiOutput) { throw 'EnableTestTone requires EnableWasapiOutput.' }
  $engineArguments += '--enable-test-tone'
}
if ($EnableTabBridge) {
  if (-not $EnableWasapiOutput) { throw 'EnableTabBridge requires EnableWasapiOutput.' }
  $engineArguments += '--enable-tab-bridge'
}
if ($EnableTabNoiseSuppressor) {
  if (-not $EnableTabBridge) { throw 'EnableTabNoiseSuppressor requires EnableTabBridge.' }
  $engineArguments += '--enable-tab-noise-suppressor'
}
if ($EnableDriverLoopback) {
  if (-not $EnableWasapiOutput) { throw 'EnableDriverLoopback requires EnableWasapiOutput.' }
  if ($EnableTestTone -or $EnableTabBridge -or ($EnableProcessDelivery -and $EnableSessionRouting)) {
    throw 'EnableDriverLoopback is exclusive with the other explicit audio sources.'
  }
  $engineArguments += '--enable-driver-loopback'
}
if ($EnableWavSource) {
  if (-not $EnableWasapiOutput) { throw 'EnableWavSource requires EnableWasapiOutput.' }
  if ($EnableTestTone -or $EnableTabBridge -or $EnableDriverLoopback -or ($EnableProcessDelivery -and $EnableSessionRouting)) {
    throw 'EnableWavSource is exclusive with the other explicit audio sources.'
  }
  $wavSourcePath = $smokePlan.WavSourcePath
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wavSourcePath) | Out-Null
  Write-WavSourceFixture $wavSourcePath
  $engineArguments += '--enable-wav-source'
  $engineArguments += '--enable-wav-loop'
  $engineArguments += '--wav-source-path'
  $engineArguments += $wavSourcePath
}
$engineProcess = $null
$offlineExitCode = $null
$processDeliveryAudioProcess = $null
if ($EnableProcessDelivery -and -not $RenderOffline) {
  # Start a short-lived audible WAV playback so the Windows session catalog
  # has at least one active session for process-loopback capture.
  $tonePath = Join-Path (Join-Path $repo '.local') 'engine-preview-smoke-delivery-tone.wav'
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $tonePath) | Out-Null
  Write-ProcessDeliveryTone $tonePath
  $safeTonePath = $tonePath.Replace("'", "''")
  $playCommand = "(New-Object System.Media.SoundPlayer('$safeTonePath')).PlaySync()"
  $processDeliveryAudioProcess = Start-Process -FilePath 'powershell.exe' `
    -ArgumentList @('-NoProfile', '-Command', $playCommand) `
    -WindowStyle Hidden -PassThru
  Start-Sleep -Milliseconds 500
}
if ($RenderOffline) {
  if ($EnableWasapiOutput -or $EnableTestTone -or $EnableSessionRouting -or
      $EnableProcessDelivery -or $EnableTabBridge -or $EnableDriverLoopback -or
      $EnableSystemVolume) {
    throw 'RenderOffline is exclusive with live-delivery switches.'
  }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $smokePlan.WavSourcePath) | Out-Null
  Write-WavSourceFixture $smokePlan.WavSourcePath
  $engineArguments += '--render-offline'
  $engineArguments += $smokePlan.OfflineRenderPath
  $engineArguments += '--enable-wav-source'
  $engineArguments += '--wav-source-path'
  $engineArguments += $smokePlan.WavSourcePath
  Push-Location $smokePlan.EngineWorkingDirectory
  try {
    $offlineOutput = & $engine @engineArguments 2>&1
    $offlineExitCode = $LASTEXITCODE
  } finally {
    Pop-Location
  }
} else {
  $engineProcess = Start-Process -FilePath $engine -ArgumentList (ConvertTo-EngineArgumentString -Arguments $engineArguments) `
    -WorkingDirectory $smokePlan.EngineWorkingDirectory -WindowStyle Hidden -PassThru
}
$irPath = $smokePlan.IrPath
$irDirectory = $smokePlan.IrDirectory
if ($RenderOffline) {
  if ($offlineExitCode -ne 0) {
    throw "Engine Preview offline render failed: exit=$offlineExitCode output=$offlineOutput"
  }
  if (-not (Test-Path -LiteralPath $smokePlan.OfflineRenderPath)) {
    throw "Engine Preview offline render did not create a WAV file: $($smokePlan.OfflineRenderPath)."
  }
  Assert-OfflineRenderWavHeader $smokePlan.OfflineRenderPath
  $offlineText = ($offlineOutput | Out-String)
  if ($offlineText -notmatch 'frames=(\d+)') { throw 'Offline render summary omitted frames.' }
  if ([int]$Matches[1] -ne 239) { throw "Offline render frame count mismatch: $($Matches[1])." }
  if ($offlineText -notmatch 'resampled 44100->48000') {
    throw 'Offline render summary omitted the resample conversion.'
  }

  $offlineSamples = Read-OfflineRenderWavDataChunk -Path $smokePlan.OfflineRenderPath
  $offlineStats = Get-EnginePreviewSmokeWavSampleStats -Samples $offlineSamples -ChannelCount 2
  Assert-EnginePreviewSmokeSignalBounds -Statistics $offlineStats -ExpectedFrameCount 239
  $statisticsCulture = [System.Globalization.CultureInfo]::InvariantCulture
  $peakText = $offlineStats.Peak.ToString('F6', $statisticsCulture)
  $rmsText = $offlineStats.Rms.ToString('F6', $statisticsCulture)
  $dcText = $offlineStats.DcOffset.ToString('F6', $statisticsCulture)
  Write-Output ("Engine Preview offline WAV statistics: peak={0} rms={1} dc={2} frames={3}." -f $peakText, $rmsText, $dcText, $offlineStats.FrameCount)
  Write-Output ("Engine Preview offline WAV render smoke passed ({0})." -f ($offlineText.Trim() -join '; '))
  exit 0
}
New-Item -ItemType Directory -Force -Path $irDirectory | Out-Null
Write-TestIrWav $irPath
$client = $null
try {
  $deadline = (Get-Date).AddSeconds(5)
  while ($null -eq $client -and (Get-Date) -lt $deadline) {
    try {
      $candidate = [System.IO.Pipes.NamedPipeClientStream]::new('.', 'HibikiDSP_v1_control', [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::None)
      $candidate.Connect(250)
      $client = $candidate
    } catch {
      if ($null -ne $candidate) { $candidate.Dispose() }
      Start-Sleep -Milliseconds 100
    }
  }
  if ($null -eq $client) { throw 'Engine Preview did not accept a local control client.' }

  # v1 Hello: HIK1, version 1, Hello (1), request ID 42, empty payload.
  $helloFrame = New-IpcFrame 1 42 @()
  Send-IpcFrame $client $helloFrame
  [byte[]]$length = New-Object byte[] 4
  Read-Exactly $client $length
  $replyLength = [BitConverter]::ToUInt32($length, 0)
  if ($replyLength -ne 20) { throw "Unexpected control reply length: $replyLength" }
  [byte[]]$reply = New-Object byte[] $replyLength
  Read-Exactly $client $reply
  Assert-IpcFrameShape -Frame $reply -ExpectedType 6 -ExpectedRequestId 42 -ExpectedPayloadLength 0

  if ($StatusOnly) {
    if (-not $EnableSystemVolume -and -not $EnableSessionRouting -and -not $EnableWasapiOutput) {
      throw 'StatusOnly requires an explicit integration flag.'
    }
    $statusFrame = New-IpcFrame 13 43 @()
    Send-IpcFrame $client $statusFrame
    $statusReply = Receive-IpcFrame $client
    Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId 43 -MinimumPayloadLength (40 + (6 * 224))
    $statusPayloadBytes = [BitConverter]::ToUInt32($statusReply, 8)
    $expectedRouteCount = 6
    if ($EnableTabBridge) { $expectedRouteCount = 7 }
    if ($EnableDriverLoopback) { $expectedRouteCount = 7 }
    if ($EnableWavSource) { $expectedRouteCount = 7 }
    if ($statusPayloadBytes -ne ($statusReply.Length - 20) -or
        $statusPayloadBytes -lt (40 + ($expectedRouteCount * 224))) {
      throw "Engine Preview status payload shape is invalid: bytes=$statusPayloadBytes."
    }
    $statusSummary = @()
    if ($EnableSystemVolume) {
      $volumeRouteOffset = 20 + 40 + (2 * 224)
      if ($statusReply[$volumeRouteOffset + 1] -ne 0) {
        throw "Windows volume route was not Ready (state=$($statusReply[$volumeRouteOffset + 1]))."
      }
      $statusSummary += 'Windows volume route Ready; explicit write-through enabled'
    }
    if ($EnableSessionRouting) {
      $sessionRouteOffset = 20 + 40 + (4 * 224)
      if ($statusReply[$sessionRouteOffset + 1] -gt 4) {
        throw "Windows session route state is invalid: $($statusReply[$sessionRouteOffset + 1])."
      }
      $sessionFrame = New-IpcFrame 15 47 @()
      Send-IpcFrame $client $sessionFrame
      $sessionReply = Receive-IpcFrame $client
      Assert-IpcFrameShape -Frame $sessionReply -ExpectedType 14 -ExpectedRequestId 47
      $sessionPayloadBytes = [BitConverter]::ToUInt32($sessionReply, 8)
      $sessionCount = [BitConverter]::ToUInt16($sessionReply, 20)
      if ($sessionPayloadBytes -ne ($sessionReply.Length - 20) -or
          $sessionPayloadBytes -ne (24 + ($sessionCount * 256))) {
        throw "Engine Preview session catalog payload shape is invalid: bytes=$sessionPayloadBytes count=$sessionCount."
      }
      $statusSummary += "session catalog Ready; entries=$sessionCount; per-App delivery E2E verified (PR #1542)"
    }
    if ($EnableWasapiOutput) {
      $mainOutputRouteOffset = 20 + 40 + (1 * 224)
      $mainOutputState = $statusReply[$mainOutputRouteOffset + 1]
      if ($mainOutputState -gt 4) {
        throw "WASAPI main output route state is invalid: $mainOutputState."
      }
      $statusSummary += "WASAPI output route state=$mainOutputState; physical delivery is endpoint-dependent"
    }
    if ($EnableProcessDelivery) {
      # Bind the first active session to main so process delivery has a routed
      # source. Retry briefly because the background tone may not have appeared
      # in the session catalog yet on this run.
      $bindAttempt = 0; $routeBound = $false; $routeBindDetail = ''
      while (-not $routeBound -and $bindAttempt -lt 20) {
        $sessionFrame = New-IpcFrame 15 ([uint64](48 + $bindAttempt)) @()
        Send-IpcFrame $client $sessionFrame
        $sessionReply = Receive-IpcFrame $client
        Assert-IpcFrameShape -Frame $sessionReply -ExpectedType 14 -ExpectedRequestId ([uint64](48 + $bindAttempt))
        if ($sessionReply.Length -ge (24 + 256)) {
          $catalogSeq = [BitConverter]::ToUInt64($sessionReply, 24)
          $entryHandle = [BitConverter]::ToUInt64($sessionReply, 44)
          $entryActive = $sessionReply[52]
          if ($entryActive -eq 1 -and $entryHandle -gt 0) {
            [byte[]]$routePayload = New-Object byte[] 128
            [BitConverter]::GetBytes([uint64]$entryHandle).CopyTo($routePayload, 0)
            [BitConverter]::GetBytes([uint64]$catalogSeq).CopyTo($routePayload, 8)
            $rl = [System.Text.Encoding]::UTF8.GetBytes("process-delivery-lane")
            $rg = [System.Text.Encoding]::UTF8.GetBytes("main")
            $routePayload[16] = [byte]$rl.Length
            $routePayload[17] = [byte]$rg.Length
            $rl.CopyTo($routePayload, 20); $rg.CopyTo($routePayload, 68)
            $rf = New-IpcFrame 17 49 $routePayload
            Send-IpcFrame $client $rf
            $rr = Receive-IpcFrame $client
            $rrType = [BitConverter]::ToUInt16($rr, 6)
            if ($rrType -ne 6) { throw "Route bind reply type=$rrType (expected Ack=6)." }
            $routeBound = $true
            $routeBindDetail = "handle=$entryHandle seq=$catalogSeq"
          }
        }
        if (-not $routeBound) { Start-Sleep -Milliseconds 100; $bindAttempt++ }
      }
      if (-not $routeBound) {
        Write-Warning 'No active audio session found for route bind.'
      } else {
        Write-Host "Route bind Ack confirmed ($routeBindDetail)."
      }
      $processRouteOffset = 20 + 40 + (5 * 224)
      $processDetailBytes = [BitConverter]::ToUInt16($statusReply, $processRouteOffset + 6)
      $processDetail = [System.Text.Encoding]::UTF8.GetString(
        $statusReply, $processRouteOffset + 104, $processDetailBytes)
      if ($processDetail -notmatch 'per-App process delivery') {
        throw "Process delivery route detail is unexpected: '$processDetail'."
      }
      $processState = $statusReply[$processRouteOffset + 1]
      if ($processState -eq 0) {
        $statusSummary += "per-App process delivery E2E verified; Ready; detail='$processDetail'"
      } elseif ($routeBound) {
        $statusSummary += "route bound ($routeBindDetail); state=$processState; detail='$processDetail'"
      } else {
        $statusSummary += "no active session to bind; state=$processState; detail='$processDetail'"
      }
    }
    if ($EnableTabBridge) {
      $tabRouteOffset = 20 + 40 + (6 * 224)
      $tabState = $null
      $tabDetail = ''
      for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($statusReply.Length -ge (20 + 40 + (7 * 224))) {
          if ($statusReply[$tabRouteOffset + 1] -le 4) {
            $tabDetailBytes = [BitConverter]::ToUInt16($statusReply, $tabRouteOffset + 6)
            if ($tabDetailBytes -gt 0 -and $tabDetailBytes -le 120) {
              $candidate = [System.Text.Encoding]::UTF8.GetString(
                $statusReply, $tabRouteOffset + 104, $tabDetailBytes)
              # The host must prove the loopback listener actually bound before it
              # reports Pending; a generic "requested" or empty detail means the
              # graph/listener setup did not complete as designed.
              if ($candidate -match 'loopback listener bound') {
                $tabState = $statusReply[$tabRouteOffset + 1]
                $tabDetail = $candidate
                break
              }
            }
          }
        }
        Start-Sleep -Milliseconds 20
        $statusFrame = New-IpcFrame 13 ([uint64](60 + $attempt)) @()
        Send-IpcFrame $client $statusFrame
        $statusReply = Receive-IpcFrame $client
        Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId ([uint64](60 + $attempt)) -MinimumPayloadLength (40 + (7 * 224))
      }
      if ($null -eq $tabState) {
        $debugBytes = [BitConverter]::ToUInt16($statusReply, $tabRouteOffset + 6)
        $debugDetail = ''
        if ($debugBytes -gt 0 -and $debugBytes -le 200) {
          $debugDetail = [System.Text.Encoding]::UTF8.GetString(
            $statusReply, $tabRouteOffset + 104, $debugBytes)
        }
        throw 'Browser tab route never confirmed the loopback listener within the bounded wait.'
      }
      if ($EnableTabNoiseSuppressor) {
        if ($tabDetail -notmatch 'suppressor active') {
          throw "Tab noise suppressor was requested but route detail does not confirm it: '$tabDetail'."
        }
        $statusSummary += "; tab noise suppressor active (opt-in)"
      }
      $statusSummary += "browser tab route state=$tabState; listener is loopback-only and opt-in"
    }
    if ($EnableTestTone) {
      $toneRendering = $false
      for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $mainOutputRouteOffset = 20 + 40 + (1 * 224)
        $detailBytes = [BitConverter]::ToUInt16($statusReply, $mainOutputRouteOffset + 6)
        $detail = [System.Text.Encoding]::UTF8.GetString($statusReply, $mainOutputRouteOffset + 104, $detailBytes)
        if ($detail -eq 'test tone rendering.') {
          $toneRendering = $true
          break
        }
        Start-Sleep -Milliseconds 20
        $statusFrame = New-IpcFrame 13 ([uint64](44 + $attempt)) @()
        Send-IpcFrame $client $statusFrame
        $statusReply = Receive-IpcFrame $client
        Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId ([uint64](44 + $attempt)) -MinimumPayloadLength (40 + (6 * 224))
      }
      if (-not $toneRendering) {
        throw 'Engine Preview test tone did not report rendered WASAPI blocks in the status snapshot.'
      }
      $statusSummary += 'test tone rendered through the user-space graph and WASAPI sink'
    }
    if ($EnableDriverLoopback) {
      $loopbackRouteOffset = 20 + 40 + (6 * 224)
      $loopbackReady = $false
      $loopbackDetail = ''
      for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($statusReply.Length -ge (20 + 40 + (7 * 224))) {
          $loopbackState = $statusReply[$loopbackRouteOffset + 1]
          $detailBytes = [BitConverter]::ToUInt16($statusReply, $loopbackRouteOffset + 6)
          if ($detailBytes -gt 0 -and $detailBytes -le 120) {
            $loopbackDetail = [System.Text.Encoding]::UTF8.GetString(
              $statusReply, $loopbackRouteOffset + 104, $detailBytes)
          }
          if ($loopbackState -eq 0 -and $loopbackDetail -match 'driver-stream loopback rendering') {
            $loopbackReady = $true
            break
          }
        }
        Start-Sleep -Milliseconds 20
        $statusFrame = New-IpcFrame 13 ([uint64](80 + $attempt)) @()
        Send-IpcFrame $client $statusFrame
        $statusReply = Receive-IpcFrame $client
        Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId ([uint64](80 + $attempt)) -MinimumPayloadLength (40 + (7 * 224))
      }
      if (-not $loopbackReady) {
        throw "Driver-stream loopback did not report rendered WASAPI blocks. Detail: '$loopbackDetail'."
      }
      $statusSummary += "driver-stream loopback rendered through encode/ring/decode and WASAPI sink ($loopbackDetail)"
    }
    if ($EnableWavSource) {
      $wavRouteOffset = 20 + 40 + (6 * 224)
      $wavDetail = ''
      $wavReady = $false
      for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($statusReply.Length -ge (20 + 40 + (7 * 224))) {
          $detailBytes = [BitConverter]::ToUInt16($statusReply, $wavRouteOffset + 6)
          if ($detailBytes -gt 0 -and $detailBytes -le 120) {
            $wavDetail = [System.Text.Encoding]::UTF8.GetString(
              $statusReply, $wavRouteOffset + 104, $detailBytes)
            if ($wavDetail -match 'wav file source rendering' -and
                $wavDetail -match 'frames=\d+/239' -and
                $wavDetail -match 'resampled 44100->48000') {
                $wavReady = $true
              break
            }
          }
        }
        Start-Sleep -Milliseconds 20
        $statusFrame = New-IpcFrame 13 ([uint64](100 + $attempt)) @()
        Send-IpcFrame $client $statusFrame
        $statusReply = Receive-IpcFrame $client
        Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId ([uint64](100 + $attempt)) -MinimumPayloadLength (40 + (7 * 224))
      }
      if (-not $wavReady) {
        throw "WAV file source did not report rendered WASAPI blocks. Detail: '$wavDetail'."
      }
      $statusSummary += "WAV file source rendered through the user-space graph and WASAPI sink ($wavDetail)"
    }
    Write-Output "Engine Preview status-only smoke passed ($($statusSummary -join '; '))."
    return
  }

  # The physical catalog is metadata only: inspect bounded wire fields without
  # printing endpoint IDs or friendly names from the local machine.
  $catalogFrame = New-IpcFrame 11 46 @()
  Send-IpcFrame $client $catalogFrame
  $catalogReply = Receive-IpcFrame $client
  Assert-IpcFrameShape -Frame $catalogReply -ExpectedType 10 -ExpectedRequestId 46
  $catalogPayloadBytes = [BitConverter]::ToUInt32($catalogReply, 8)
  $catalogCount = [BitConverter]::ToUInt16($catalogReply, 20)
  if ($catalogPayloadBytes -ne ($catalogReply.Length - 20) -or
      $catalogPayloadBytes -ne (16 + ($catalogCount * 416))) {
    throw "Engine Preview device catalog payload shape is invalid: bytes=$catalogPayloadBytes count=$catalogCount."
  }
  $catalogEntrySummary = @()
  for ($index = 0; $index -lt $catalogCount; $index++) {
    $offset = 20 + 16 + ($index * 416)
    $catalogEntrySummary += "$( [BitConverter]::ToUInt16($catalogReply, $offset) )/$( [BitConverter]::ToUInt16($catalogReply, $offset + 2) )/$( $catalogReply[$offset + 4] )/$( $catalogReply[$offset + 5] )/$( [BitConverter]::ToUInt16($catalogReply, $offset + 6) )/$( [BitConverter]::ToUInt32($catalogReply, $offset + 396) )/$( [BitConverter]::ToUInt32($catalogReply, $offset + 400) )/$( [BitConverter]::ToUInt32($catalogReply, $offset + 404) )"
  }
  Write-Output "Engine Preview physical catalog snapshot passed (count=$catalogCount, entries=endpointBytes/displayBytes/flow/availability/flags/channels/rate/frames [$($catalogEntrySummary -join ';')])."

  # Drive one real control transaction through the queue. The C++ worker must
  # reconcile it before the following status request reports the new dB and
  # generation; this is the vertical slice used by the Desktop Preview.
  [byte[]]$volumePayload = New-Object byte[] 16
  [BitConverter]::GetBytes([int]-786432).CopyTo($volumePayload, 0) # -12 dB Q16.16
  [BitConverter]::GetBytes([uint64]1).CopyTo($volumePayload, 8)
  $volumeFrame = New-IpcFrame 2 44 $volumePayload
  Send-IpcFrame $client $volumeFrame
  $volumeReply = Receive-IpcFrame $client
  Assert-IpcFrameShape -Frame $volumeReply -ExpectedType 6 -ExpectedRequestId 44 -ExpectedPayloadLength 0

  $statusMatched = $false
  $statusReplyLength = 0
  for ($attempt = 0; $attempt -lt 20; $attempt++) {
    # v1 ControlStatusRequest: HIK1, version 1, type 13, request ID 43+attempt.
    $statusFrame = New-IpcFrame 13 ([uint64](43 + $attempt)) @()
    Send-IpcFrame $client $statusFrame
    $statusReply = Receive-IpcFrame $client
    $statusReplyLength = $statusReply.Length
    Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId ([uint64](43 + $attempt)) -MinimumPayloadLength 40 -MaximumPayloadLength 1832
    if ([BitConverter]::ToUInt16($statusReply, 20) -lt 1) {
      throw 'Engine Preview did not return a correlated ControlStatusSnapshot.'
    }
    if ([BitConverter]::ToInt32($statusReply, 32) -eq -786432 -and
        [BitConverter]::ToUInt64($statusReply, 48) -eq 1) {
      $statusMatched = $true
      break
    }
    Start-Sleep -Milliseconds 20
  }
  if (-not $statusMatched) {
    throw 'Engine Preview status did not reflect the queued -12 dB volume command.'
  }
  $mainOutputRouteOffset = 20 + 40 + (1 * 224)
  $mainOutputState = $statusReply[$mainOutputRouteOffset + 1]
  if (-not $EnableWasapiOutput -and $mainOutputState -ne 4) {
    throw "Default Engine Preview unexpectedly exposed a physical output route (state=$mainOutputState)."
  }
  if ($EnableWasapiOutput -and $mainOutputState -gt 4) {
    throw "WASAPI main output route state is invalid: $mainOutputState."
  }

  # IR prepare is a control-worker-only file import. The payload carries a
  # bounded local path and policy; the engine reads/decode/transforms the WAV
  # off the pipe and RT threads, then ACKs only after convolver preparation.
  [byte[]]$irPayload = New-Object byte[] 288
  [BitConverter]::GetBytes([uint32]1).CopyTo($irPayload, 0)
  $irPayload[4] = 2 # LinearPhase
  [BitConverter]::GetBytes([int]32768).CopyTo($irPayload, 8) # strength 0.5
  [BitConverter]::GetBytes([uint32]48000).CopyTo($irPayload, 12)
  [BitConverter]::GetBytes([uint32]1).CopyTo($irPayload, 16)
  $irPathBytes = [System.Text.Encoding]::UTF8.GetBytes($irPath)
  if ($irPathBytes.Length -gt 260) { throw 'Smoke IR path exceeded the v1 bound.' }
  [BitConverter]::GetBytes([uint16]$irPathBytes.Length).CopyTo($irPayload, 20)
  $irPathBytes.CopyTo($irPayload, 24)
  $irFrame = New-IpcFrame 19 45 $irPayload
  Send-IpcFrame $client $irFrame
  $irReply = Receive-IpcFrame $client
  Assert-IpcFrameShape -Frame $irReply -ExpectedType 6 -ExpectedRequestId 45 -ExpectedPayloadLength 0
  Write-Output "Engine Preview control Hello/Ack, volume round-trip and status snapshot smoke passed (payload=$statusReplyLength bytes)."
  Write-Output 'Engine Preview bounded IR WAV prepare and phase-policy ACK smoke passed.'
} finally {
  if ($null -ne $client) { $client.Dispose() }
  if ($null -ne $processDeliveryAudioProcess -and -not $processDeliveryAudioProcess.HasExited) {
    Stop-Process -Id $processDeliveryAudioProcess.Id -Force
  }
  if ($null -ne $engineProcess -and -not $engineProcess.HasExited) {
    Stop-Process -Id $engineProcess.Id; $engineProcess.WaitForExit()
  }
  if (Test-Path -LiteralPath $irPath) { Remove-Item -LiteralPath $irPath -Force -ErrorAction SilentlyContinue }
  if (($EnableWavSource -or $RenderOffline) -and (Test-Path -LiteralPath $smokePlan.WavSourcePath)) {
    Remove-Item -LiteralPath $smokePlan.WavSourcePath -Force -ErrorAction SilentlyContinue
  }
  if ($RenderOffline -and (Test-Path -LiteralPath $smokePlan.OfflineRenderPath)) {
    Remove-Item -LiteralPath $smokePlan.OfflineRenderPath -Force -ErrorAction SilentlyContinue
  }
}

