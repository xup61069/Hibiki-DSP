[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $repo '.local/engine-preview/Release/hibiki_engine_preview.exe'
if (-not (Test-Path -LiteralPath $engine)) {
  throw "Build Engine Preview first: pwsh -File tools/build-engine-preview.ps1"
}

function Read-Exactly([System.IO.Stream]$Stream, [byte[]]$Buffer) {
  $offset = 0
  while ($offset -lt $Buffer.Length) {
    $read = $Stream.Read($Buffer, $offset, $Buffer.Length - $offset)
    if ($read -eq 0) { throw 'Engine Preview closed the control pipe early.' }
    $offset += $read
  }
}

$engineProcess = Start-Process -FilePath $engine -WorkingDirectory (Split-Path $engine) -WindowStyle Hidden -PassThru
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

  # v1 ControlStatusRequest: HIK1, version 1, type 13, request ID 43.
  [byte[]]$statusRequest = 0x48,0x49,0x4B,0x31,0x01,0x00,0x0D,0x00,0x00,0x00,0x00,0x00,0x2B,0x00,0x00,0x00,0x00,0x00,0x00,0x00
  $client.Write([BitConverter]::GetBytes([uint32]$statusRequest.Length), 0, 4)
  $client.Write($statusRequest, 0, $statusRequest.Length)
  $client.Flush()
  [byte[]]$statusLength = New-Object byte[] 4
  Read-Exactly $client $statusLength
  $statusReplyLength = [BitConverter]::ToUInt32($statusLength, 0)
  [byte[]]$statusReply = New-Object byte[] $statusReplyLength
  Read-Exactly $client $statusReply
  if ($statusReplyLength -lt 60 -or $statusReplyLength -gt 1852 -or
      [BitConverter]::ToUInt32($statusReply, 8) -ne ($statusReplyLength - 20)) {
    throw "Unexpected control status reply length: $statusReplyLength"
  }
  if ($statusReply[0] -ne 0x48 -or $statusReply[1] -ne 0x49 -or $statusReply[2] -ne 0x4B -or
      $statusReply[3] -ne 0x31 -or $statusReply[6] -ne 12 -or
      [BitConverter]::ToUInt64($statusReply, 12) -ne 43 -or
      [BitConverter]::ToUInt16($statusReply, 20) -lt 1) {
    throw 'Engine Preview did not return a correlated ControlStatusSnapshot.'
  }
  Write-Output "Engine Preview control Hello/Ack and status snapshot smoke passed (payload=$statusReplyLength bytes)."
} finally {
  if ($null -ne $client) { $client.Dispose() }
  if (-not $engineProcess.HasExited) { Stop-Process -Id $engineProcess.Id; $engineProcess.WaitForExit() }
}
