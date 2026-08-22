[CmdletBinding()]
param(
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting,
  [switch]$EnableWasapiOutput,
  [switch]$StatusOnly,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-EnginePreviewSmokePlan {
  param(
    [Parameter(Mandatory)][string]$RepositoryRoot
  )

  $localRoot = Join-Path $RepositoryRoot '.local'
  $engineWorkingDirectory = Join-Path $localRoot 'engine-preview/Release'
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    LocalRoot = $localRoot
    EngineWorkingDirectory = $engineWorkingDirectory
    EnginePath = Join-Path $engineWorkingDirectory 'hibiki_engine_preview.exe'
    IrDirectory = $localRoot
    IrPath = Join-Path $localRoot 'engine-preview-smoke-ir.wav'
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
    [hashtable]$SyntheticAttributes
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if (-not (Test-Path -LiteralPath $fullPath)) { return $null }
  return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force).Attributes
}

function Assert-EnginePreviewSmokePath {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-EnginePreviewSmokePathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Engine Preview smoke path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-EnginePreviewSmokeExistingAttributes -Path $fullPath -SyntheticAttributes $SyntheticAttributes
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Engine Preview smoke $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-EnginePreviewSmokeExistingAttributes -Path $cursor -SyntheticAttributes $SyntheticAttributes
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

  $constructionCaught = $false
  try { [void](New-IpcFrame 1 42 (New-Object byte[] 1048577)) } catch { $constructionCaught = $true }
  if (-not $constructionCaught) { throw 'IPC frame self-test expected constructor bound rejection.' }
  $cases++

  Write-Output "Engine Preview path and IPC self-test passed ($cases cases; offline/no-process/no-file-write)."
  exit 0
}

$smokePlan = Get-EnginePreviewSmokePlan -RepositoryRoot $repo
Assert-EnginePreviewSmokePath -Path $smokePlan.EnginePath -Root $smokePlan.LocalRoot -Kind File
Assert-EnginePreviewSmokePath -Path $smokePlan.EngineWorkingDirectory -Root $smokePlan.LocalRoot -Kind Directory
Assert-EnginePreviewSmokePath -Path $smokePlan.IrDirectory -Root $smokePlan.RepositoryRoot -Kind Directory -AllowMissingLeaf
Assert-EnginePreviewSmokePath -Path $smokePlan.IrPath -Root $smokePlan.RepositoryRoot -Kind File -AllowMissingLeaf
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

$engineArguments = @()
if ($EnableSystemVolume) { $engineArguments += '--enable-system-volume' }
if ($EnableSessionRouting) { $engineArguments += '--enable-session-routing' }
if ($EnableWasapiOutput) { $engineArguments += '--enable-wasapi-output' }
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments `
  -WorkingDirectory $smokePlan.EngineWorkingDirectory -WindowStyle Hidden -PassThru
$irPath = $smokePlan.IrPath
$irDirectory = $smokePlan.IrDirectory
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
    if ($statusPayloadBytes -ne ($statusReply.Length - 20) -or
        $statusPayloadBytes -lt (40 + (6 * 224))) {
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
      $statusSummary += "session catalog Ready; entries=$sessionCount; per-App delivery unverified"
    }
    if ($EnableWasapiOutput) {
      $mainOutputRouteOffset = 20 + 40 + (1 * 224)
      $mainOutputState = $statusReply[$mainOutputRouteOffset + 1]
      if ($mainOutputState -gt 4) {
        throw "WASAPI main output route state is invalid: $mainOutputState."
      }
      $statusSummary += "WASAPI output route state=$mainOutputState; physical delivery is endpoint-dependent"
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
  if (-not $engineProcess.HasExited) { Stop-Process -Id $engineProcess.Id; $engineProcess.WaitForExit() }
  if (Test-Path -LiteralPath $irPath) { Remove-Item -LiteralPath $irPath -Force -ErrorAction SilentlyContinue }
}
