[CmdletBinding()]
param(
  [ValidateRange(1, 100)]
  [int]$Iterations = 3,
  [ValidateRange(0, 60000)]
  [int]$IntervalMs = 0,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-EnginePreviewSoakPlan {
  param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][int]$Iterations,
    [Parameter(Mandatory)][int]$IntervalMs
  )

  $localRoot = Join-Path $RepositoryRoot '.local'
  $buildRoot = Join-Path $localRoot 'engine-preview'
  $reportDirectory = Join-Path $localRoot 'engine-preview-soak'
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    LocalRoot = $localRoot
    BuildRoot = $buildRoot
    EngineWorkingDirectory = $buildRoot
    EnginePath = Join-Path $buildRoot 'Release/hibiki_engine_preview.exe'
    ReportDirectory = $reportDirectory
    ReportPath = Join-Path $reportDirectory 'report.json'
    Iterations = $Iterations
    IntervalMs = $IntervalMs
  }
}

function Test-EnginePreviewSoakPathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd([char]'\', [char]'/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([char]'\', [char]'/')
  return $fullPath -eq $fullRoot -or
    $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar,
                          [StringComparison]::OrdinalIgnoreCase)
}

function Get-EnginePreviewSoakExistingAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path)
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Engine Preview soak path inspection failed: $fullPath"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Engine Preview soak path inspection failed: $fullPath"
  }
}

function Assert-EnginePreviewSoakPath {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][ValidateSet('File', 'Directory')][string]$Kind,
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd([char]'\', [char]'/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([char]'\', [char]'/')
  if (-not (Test-EnginePreviewSoakPathUnderRoot -Path $fullPath -Root $fullRoot)) {
    throw "Engine Preview soak path must remain under the expected root: $fullPath"
  }

  $leafAttributes = Get-EnginePreviewSoakExistingAttributes -Path $fullPath `
    -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes -and -not $AllowMissingLeaf) {
    throw "Engine Preview soak required $Kind does not exist: $fullPath"
  }

  $cursor = $fullPath
  while ($true) {
    $attributes = Get-EnginePreviewSoakExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Engine Preview soak path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $fullPath) {
        $isDirectory = ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0
        if ($Kind -eq 'Directory' -and -not $isDirectory) {
          throw "Engine Preview soak path is not a directory: $fullPath"
        }
        if ($Kind -eq 'File' -and $isDirectory) {
          throw "Engine Preview soak path is not a file: $fullPath"
        }
      }
    }
    if ($cursor -eq $fullRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd([char]'\', [char]'/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Engine Preview soak path could not reach the expected root: $fullPath"
    }
    $cursor = $parent
  }
}

function Read-Exactly([System.IO.Stream]$Stream, [byte[]]$Buffer) {
  $offset = 0
  while ($offset -lt $Buffer.Length) {
    $read = $Stream.Read($Buffer, $offset, $Buffer.Length - $offset)
    if ($read -eq 0) { throw 'Engine Preview soak control stream closed early.' }
    $offset += $read
  }
}

function New-IpcFrame([uint16]$Type, [uint64]$RequestId, [byte[]]$Payload) {
  if ($null -eq $Payload) { $Payload = @() }
  if ($Payload.Length -gt 1048576) {
    throw "Engine Preview soak IPC payload exceeds the v1 bound: $($Payload.Length)"
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
    [Parameter(Mandatory)][byte[]]$Frame,
    [Parameter(Mandatory)][uint16]$ExpectedType,
    [Parameter(Mandatory)][uint64]$ExpectedRequestId,
    [int]$ExpectedPayloadLength = -1,
    [uint32]$MinimumPayloadLength = 0,
    [uint32]$MaximumPayloadLength = 1048576
  )

  if ($null -eq $Frame -or $Frame.Length -lt 20) {
    throw 'Engine Preview soak IPC frame is shorter than the v1 header.'
  }
  if ([BitConverter]::ToUInt32($Frame, 0) -ne [uint32]0x314B4948 -or
      [BitConverter]::ToUInt16($Frame, 4) -ne [uint16]1) {
    throw 'Engine Preview soak IPC magic or version is invalid.'
  }
  $payloadLength = [BitConverter]::ToUInt32($Frame, 8)
  if ($payloadLength -gt $MaximumPayloadLength) {
    throw "Engine Preview soak IPC payload exceeds its bound: $payloadLength"
  }
  if ($Frame.Length -ne (20 + [int]$payloadLength)) {
    throw 'Engine Preview soak IPC frame length does not match its payload.'
  }
  if ([BitConverter]::ToUInt16($Frame, 6) -ne $ExpectedType) {
    throw 'Engine Preview soak IPC frame type mismatch.'
  }
  if ([BitConverter]::ToUInt64($Frame, 12) -ne $ExpectedRequestId) {
    throw 'Engine Preview soak IPC request correlation mismatch.'
  }
  if ($ExpectedPayloadLength -ge 0 -and $payloadLength -ne [uint32]$ExpectedPayloadLength) {
    throw 'Engine Preview soak IPC payload length mismatch.'
  }
  if ($payloadLength -lt $MinimumPayloadLength) {
    throw 'Engine Preview soak IPC payload is too short.'
  }
}

function Send-IpcFrame([System.IO.Stream]$Stream, [byte[]]$Frame) {
  $length = [BitConverter]::GetBytes([uint32]$Frame.Length)
  $Stream.Write($length, 0, $length.Length)
  $Stream.Write($Frame, 0, $Frame.Length)
  $Stream.Flush()
}

function Receive-IpcFrame([System.IO.Stream]$Stream) {
  [byte[]]$lengthPrefix = New-Object byte[] 4
  Read-Exactly $Stream $lengthPrefix
  $frameLength = [BitConverter]::ToUInt32($lengthPrefix, 0)
  if ($frameLength -lt 20 -or $frameLength -gt 1048596) {
    throw "Engine Preview soak received an invalid frame length: $frameLength"
  }
  [byte[]]$frame = New-Object byte[] $frameLength
  Read-Exactly $Stream $frame
  return ,$frame
}

function Assert-EnginePreviewSoakArguments {
  param(
    [Parameter(Mandatory)][int]$Iterations,
    [Parameter(Mandatory)][int]$IntervalMs
  )

  if ($Iterations -lt 1 -or $Iterations -gt 100) {
    throw "Engine Preview soak iteration count is out of bounds: $Iterations"
  }
  if ($IntervalMs -lt 0 -or $IntervalMs -gt 60000) {
    throw "Engine Preview soak interval is out of bounds: $IntervalMs ms"
  }
}

function Get-EnginePreviewSoakAggregate {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Results,
    [Parameter(Mandatory)][int]$RequestedIterations
  )

  $materialized = @($Results)
  $passed = @($materialized | Where-Object { $_.result -eq 'pass' }).Count
  $failed = @($materialized | Where-Object { $_.result -eq 'fail' }).Count
  $durations = @($materialized | ForEach-Object { [double]$_.duration_ms })
  $minimum = $null
  $maximum = $null
  $average = $null
  $total = 0
  if ($durations.Count -gt 0) {
    $summary = $durations | Measure-Object -Minimum -Maximum -Sum -Average
    $minimum = [math]::Round($summary.Minimum, 1)
    $maximum = [math]::Round($summary.Maximum, 1)
    $average = [math]::Round($summary.Average, 1)
    $total = [math]::Round($summary.Sum, 1)
  }
  return [pscustomobject]@{
    requested_iterations = $RequestedIterations
    completed_iterations = $materialized.Count
    passed_iterations = $passed
    failed_iterations = $failed
    total_duration_ms = $total
    minimum_duration_ms = $minimum
    maximum_duration_ms = $maximum
    average_duration_ms = $average
  }
}

function Get-EnginePreviewSoakCleanupDecision {
  param(
    [Parameter(Mandatory)][bool]$HasControlClient,
    [Parameter(Mandatory)][bool]$EngineProcessHasExited
  )

  return [pscustomobject]@{
    DisposeControlClient = $HasControlClient
    StopEngineProcess = -not $EngineProcessHasExited
    BoundedWaitForEngineExit = $true
  }
}

function Invoke-EnginePreviewSoakSelfTest {
  $cases = 0
  Assert-EnginePreviewSoakArguments -Iterations 1 -IntervalMs 0; $cases++
  Assert-EnginePreviewSoakArguments -Iterations 100 -IntervalMs 60000; $cases++

  $iterationCaught = $false
  try { Assert-EnginePreviewSoakArguments -Iterations 0 -IntervalMs 0 } catch { $iterationCaught = $_.Exception.Message -match 'out of bounds' }
  if (-not $iterationCaught) { throw 'soak self-test expected a zero-iteration rejection.' }
  $cases++

  $intervalCaught = $false
  try { Assert-EnginePreviewSoakArguments -Iterations 2 -IntervalMs 60001 } catch { $intervalCaught = $_.Exception.Message -match 'out of bounds' }
  if (-not $intervalCaught) { throw 'soak self-test expected an out-of-range interval rejection.' }
  $cases++

  [byte[]]$payload = 0x10, 0x20, 0x30
  $frame = New-IpcFrame 1 42 $payload
  Assert-IpcFrameShape -Frame $frame -ExpectedType 1 -ExpectedRequestId 42 -ExpectedPayloadLength 3; $cases++

  $correlationCaught = $false
  try { Assert-IpcFrameShape -Frame $frame -ExpectedType 2 -ExpectedRequestId 43 } catch { $correlationCaught = $true }
  if (-not $correlationCaught) { throw 'soak self-test expected IPC correlation rejection.' }
  $cases++

  $shortCaught = $false
  try { Assert-IpcFrameShape -Frame ([byte[]](0, 1, 2)) -ExpectedType 1 -ExpectedRequestId 42 } catch { $shortCaught = $true }
  if (-not $shortCaught) { throw 'soak self-test expected short-frame rejection.' }
  $cases++

  $constructionCaught = $false
  try { [void](New-IpcFrame 1 42 (New-Object byte[] 1048577)) } catch { $constructionCaught = $true }
  if (-not $constructionCaught) { throw 'soak self-test expected oversized-frame construction rejection.' }
  $cases++

  $aggregate = Get-EnginePreviewSoakAggregate -Results @(
    [pscustomobject]@{ result = 'pass'; duration_ms = 100.4 },
    [pscustomobject]@{ result = 'pass'; duration_ms = 200.5 },
    [pscustomobject]@{ result = 'fail'; duration_ms = 50.0 }
  ) -RequestedIterations 5
  if ($aggregate.completed_iterations -ne 3 -or $aggregate.passed_iterations -ne 2 -or
      $aggregate.failed_iterations -ne 1 -or $aggregate.minimum_duration_ms -ne 50.0 -or
      $aggregate.maximum_duration_ms -ne 200.5 -or $aggregate.average_duration_ms -ne 117.0) {
    throw 'soak self-test found incorrect aggregate statistics.'
  }
  $cases++

  $emptyAggregate = Get-EnginePreviewSoakAggregate -Results @() -RequestedIterations 1
  if ($emptyAggregate.completed_iterations -ne 0 -or $null -ne $emptyAggregate.minimum_duration_ms) {
    throw 'soak self-test found incorrect empty aggregate handling.'
  }
  $cases++

  $decision = Get-EnginePreviewSoakCleanupDecision -HasControlClient $true -EngineProcessHasExited $false
  if (-not $decision.DisposeControlClient -or -not $decision.StopEngineProcess -or -not $decision.BoundedWaitForEngineExit) {
    throw 'soak self-test found an unsafe live cleanup decision.'
  }
  $cases++

  $exitedDecision = Get-EnginePreviewSoakCleanupDecision -HasControlClient $false -EngineProcessHasExited $true
  if ($exitedDecision.DisposeControlClient -or $exitedDecision.StopEngineProcess) {
    throw 'soak self-test found a redundant cleanup decision for an exited engine.'
  }
  $cases++

  return $cases
}

if ($SelfTest) {
  $cases = Invoke-EnginePreviewSoakSelfTest
  Write-Output "Engine Preview soak self-test passed ($cases cases; offline/no-process/no-file-write)."
  exit 0
}

$soakPlan = Get-EnginePreviewSoakPlan -RepositoryRoot $repo -Iterations $Iterations -IntervalMs $IntervalMs
Assert-EnginePreviewSoakPath -Path $soakPlan.EnginePath -Root $soakPlan.LocalRoot -Kind File
Assert-EnginePreviewSoakPath -Path $soakPlan.EngineWorkingDirectory -Root $soakPlan.LocalRoot -Kind Directory
Assert-EnginePreviewSoakPath -Path $soakPlan.ReportDirectory -Root $soakPlan.LocalRoot -Kind Directory -AllowMissingLeaf
Assert-EnginePreviewSoakPath -Path $soakPlan.ReportPath -Root $soakPlan.LocalRoot -Kind File -AllowMissingLeaf
if (@(Get-Process -Name hibiki_engine_preview -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Another Engine Preview process is already running; stop it before running a soak.'
}

$runStartedAt = (Get-Date).ToUniversalTime()
$results = @()
for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
  if ($iteration -gt 1 -and $IntervalMs -gt 0) {
    Start-Sleep -Milliseconds $IntervalMs
  }
  $startedAt = (Get-Date).ToUniversalTime()
  $engineProcess = Start-Process -FilePath $soakPlan.EnginePath `
    -WorkingDirectory $soakPlan.EngineWorkingDirectory -WindowStyle Hidden -PassThru
  $client = $null
  $succeeded = $false
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
    if ($null -eq $client) { throw 'Engine Preview soak did not accept a local control client.' }

    # v1 Hello/Ack request correlation, mirroring the existing smoke.
    $helloId = [uint64](42 + $iteration)
    Send-IpcFrame $client (New-IpcFrame 1 $helloId @())
    [byte[]]$lengthPrefix = New-Object byte[] 4
    Read-Exactly $client $lengthPrefix
    $replyLength = [BitConverter]::ToUInt32($lengthPrefix, 0)
    if ($replyLength -ne 20) { throw "Unexpected control reply length: $replyLength" }
    [byte[]]$ack = New-Object byte[] $replyLength
    Read-Exactly $client $ack
    Assert-IpcFrameShape -Frame $ack -ExpectedType 6 -ExpectedRequestId $helloId -ExpectedPayloadLength 0

    # One fixed Main volume command (-12 dB Q16.16): identical every cycle.
    [byte[]]$volumePayload = New-Object byte[] 16
    [BitConverter]::GetBytes([int]-786432).CopyTo($volumePayload, 0)
    [BitConverter]::GetBytes([uint64]1).CopyTo($volumePayload, 8)
    $volumeId = [uint64](44 + $iteration)
    Send-IpcFrame $client (New-IpcFrame 2 $volumeId $volumePayload)
    $volumeReply = Receive-IpcFrame $client
    Assert-IpcFrameShape -Frame $volumeReply -ExpectedType 6 -ExpectedRequestId $volumeId -ExpectedPayloadLength 0

    # Status convergence: the C++ worker must reconcile the queued command.
    $converged = $false
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
      $statusId = [uint64](50 + ($iteration * 20) + $attempt)
      Send-IpcFrame $client (New-IpcFrame 13 $statusId @())
      $statusReply = Receive-IpcFrame $client
      Assert-IpcFrameShape -Frame $statusReply -ExpectedType 12 -ExpectedRequestId $statusId -MinimumPayloadLength 40 -MaximumPayloadLength 1832
      if ([BitConverter]::ToUInt16($statusReply, 20) -lt 1) {
        throw 'Engine Preview soak did not return a correlated ControlStatusSnapshot.'
      }
      if ([BitConverter]::ToInt32($statusReply, 32) -eq -786432 -and
          [BitConverter]::ToUInt64($statusReply, 48) -eq 1) {
        $converged = $true
        break
      }
      Start-Sleep -Milliseconds 20
    }
    if (-not $converged) {
      throw "Engine Preview soak iteration $iteration did not converge on the queued volume command."
    }
    $succeeded = $true
  } catch {
    Write-Warning "Engine Preview soak iteration $iteration failed: $($_.Exception.Message)"
  } finally {
    $decision = Get-EnginePreviewSoakCleanupDecision `
      -HasControlClient ($null -ne $client) `
      -EngineProcessHasExited ($null -eq $engineProcess -or $engineProcess.HasExited)
    if ($decision.DisposeControlClient) {
      $client.Dispose()
      $client = $null
    }
    if ($decision.StopEngineProcess) {
      Stop-Process -Id $engineProcess.Id
      $null = $engineProcess.WaitForExit(5000)
    }
  }

  $exitCode = $null
  if ($engineProcess.HasExited) { $exitCode = $engineProcess.ExitCode }
  $results += [pscustomobject]@{
    iteration = $iteration
    result = $(if ($succeeded) { 'pass' } else { 'fail' })
    duration_ms = [math]::Round(((Get-Date).ToUniversalTime() - $startedAt).TotalMilliseconds, 1)
    exit_code = $exitCode
  }
}

$aggregate = Get-EnginePreviewSoakAggregate -Results $results -RequestedIterations $Iterations
$finishedAt = (Get-Date).ToUniversalTime()
New-Item -ItemType Directory -Force -Path $soakPlan.ReportDirectory | Out-Null
$report = [ordered]@{
  schema_version = 1
  harness = 'engine-preview-soak'
  requested_iterations = $aggregate.requested_iterations
  completed_iterations = $aggregate.completed_iterations
  passed_iterations = $aggregate.passed_iterations
  failed_iterations = $aggregate.failed_iterations
  total_duration_ms = $aggregate.total_duration_ms
  minimum_duration_ms = $aggregate.minimum_duration_ms
  maximum_duration_ms = $aggregate.maximum_duration_ms
  average_duration_ms = $aggregate.average_duration_ms
  started_at = $runStartedAt.ToString('o')
  finished_at = $finishedAt.ToString('o')
  iterations = @(
    $results | ForEach-Object {
      [ordered]@{
        iteration = $_.iteration
        result = $_.result
        duration_ms = $_.duration_ms
        exit_code = $_.exit_code
      }
    }
  )
}
$reportJson = $report | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($soakPlan.ReportPath, $reportJson, [System.Text.UTF8Encoding]::new($false))
"Engine Preview soak finished: passed=$($aggregate.passed_iterations) failed=$($aggregate.failed_iterations) average=$($aggregate.average_duration_ms) ms; anonymous report written under .local." | Write-Output
if ($aggregate.failed_iterations -gt 0) { exit 1 }
