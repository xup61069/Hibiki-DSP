[CmdletBinding()]
param(
  [switch]$EnableSystemVolume,
  [switch]$EnableSessionRouting,
  [switch]$StatusOnly
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
if (-not (Test-Path -LiteralPath $engine)) {
  throw "Build Engine Preview first: pwsh -File tools/build-engine-preview.ps1"
}
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running this smoke.'
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
  $frame = New-Object byte[] (20 + $Payload.Length)
  [BitConverter]::GetBytes([uint32]0x314B4948).CopyTo($frame, 0)
  [BitConverter]::GetBytes([uint16]1).CopyTo($frame, 4)
  [BitConverter]::GetBytes($Type).CopyTo($frame, 6)
  [BitConverter]::GetBytes([uint32]$Payload.Length).CopyTo($frame, 8)
  [BitConverter]::GetBytes($RequestId).CopyTo($frame, 12)
  if ($Payload.Length -gt 0) { $Payload.CopyTo($frame, 20) }
  return ,$frame
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
$engineProcess = Start-Process -FilePath $engine -ArgumentList $engineArguments `
  -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
$irPath = Join-Path $repo '.local/engine-preview-smoke-ir.wav'
$irDirectory = Split-Path $irPath
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
  [byte[]]$request = 0x48,0x49,0x4B,0x31,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x2A,0x00,0x00,0x00,0x00,0x00,0x00,0x00
  $client.Write([BitConverter]::GetBytes([uint32]$request.Length), 0, 4)
  $client.Write($request, 0, $request.Length)
  $client.Flush()
  [byte[]]$length = New-Object byte[] 4
  Read-Exactly $client $length
  $replyLength = [BitConverter]::ToUInt32($length, 0)
  if ($replyLength -ne 20) { throw "Unexpected control reply length: $replyLength" }
  [byte[]]$reply = New-Object byte[] $replyLength
  Read-Exactly $client $reply
  if ($reply[0] -ne 0x48 -or $reply[1] -ne 0x49 -or $reply[2] -ne 0x4B -or $reply[3] -ne 0x31 -or
      $reply[6] -ne 6 -or [BitConverter]::ToUInt64($reply, 12) -ne 42) {
    throw 'Engine Preview Hello did not receive the v1 correlated Ack.'
  }

  if ($StatusOnly) {
    if (-not $EnableSystemVolume -and -not $EnableSessionRouting) {
      throw 'StatusOnly requires -EnableSystemVolume or -EnableSessionRouting.'
    }
    $statusFrame = New-IpcFrame 13 43 @()
    Send-IpcFrame $client $statusFrame
    $statusReply = Receive-IpcFrame $client
    if ($statusReply[6] -ne 12 -or [BitConverter]::ToUInt64($statusReply, 12) -ne 43) {
      throw 'Engine Preview status-only probe did not receive a correlated snapshot.'
    }
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
      if ($sessionReply[6] -ne 14 -or [BitConverter]::ToUInt64($sessionReply, 12) -ne 47) {
        throw 'Engine Preview session catalog request did not receive a correlated snapshot.'
      }
      $sessionPayloadBytes = [BitConverter]::ToUInt32($sessionReply, 8)
      $sessionCount = [BitConverter]::ToUInt16($sessionReply, 20)
      if ($sessionPayloadBytes -ne ($sessionReply.Length - 20) -or
          $sessionPayloadBytes -ne (24 + ($sessionCount * 256))) {
        throw "Engine Preview session catalog payload shape is invalid: bytes=$sessionPayloadBytes count=$sessionCount."
      }
      $statusSummary += "session catalog Ready; entries=$sessionCount; per-App delivery unverified"
    }
    Write-Output "Engine Preview status-only smoke passed ($($statusSummary -join '; '))."
    return
  }

  # The physical catalog is metadata only: inspect bounded wire fields without
  # printing endpoint IDs or friendly names from the local machine.
  $catalogFrame = New-IpcFrame 11 46 @()
  Send-IpcFrame $client $catalogFrame
  $catalogReply = Receive-IpcFrame $client
  if ($catalogReply[6] -ne 10 -or [BitConverter]::ToUInt64($catalogReply, 12) -ne 46) {
    throw 'Engine Preview device catalog request did not receive a correlated snapshot.'
  }
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
  if ($volumeReply[6] -ne 6 -or [BitConverter]::ToUInt64($volumeReply, 12) -ne 44) {
    throw 'Engine Preview volume command did not receive the v1 correlated Ack.'
  }

  $statusMatched = $false
  $statusReplyLength = 0
  for ($attempt = 0; $attempt -lt 20; $attempt++) {
    # v1 ControlStatusRequest: HIK1, version 1, type 13, request ID 43+attempt.
    $statusFrame = New-IpcFrame 13 ([uint64](43 + $attempt)) @()
    Send-IpcFrame $client $statusFrame
    $statusReply = Receive-IpcFrame $client
    $statusReplyLength = $statusReply.Length
    if ($statusReplyLength -lt 60 -or $statusReplyLength -gt 1852 -or
        [BitConverter]::ToUInt32($statusReply, 8) -ne ($statusReplyLength - 20) -or
        $statusReply[6] -ne 12 -or
        [BitConverter]::ToUInt64($statusReply, 12) -ne [uint64](43 + $attempt) -or
        [BitConverter]::ToUInt16($statusReply, 20) -lt 1) {
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
  if ($irReply[6] -ne 6 -or [BitConverter]::ToUInt64($irReply, 12) -ne 45) {
    throw 'Engine Preview IR prepare command did not receive a correlated Ack.'
  }
  Write-Output "Engine Preview control Hello/Ack, volume round-trip and status snapshot smoke passed (payload=$statusReplyLength bytes)."
  Write-Output 'Engine Preview bounded IR WAV prepare and phase-policy ACK smoke passed.'
} finally {
  if ($null -ne $client) { $client.Dispose() }
  if (-not $engineProcess.HasExited) { Stop-Process -Id $engineProcess.Id; $engineProcess.WaitForExit() }
  if (Test-Path -LiteralPath $irPath) { Remove-Item -LiteralPath $irPath -Force -ErrorAction SilentlyContinue }
}
