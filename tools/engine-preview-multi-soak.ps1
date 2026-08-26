#Requires -Version 7
[CmdletBinding()]
param(
  [ValidateRange(1, 20)]
  [int]$Rounds = 3,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-EnginePreviewMultiSoakPlan {
  param(
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [Parameter(Mandatory)][int]$Rounds
  )

  $localRoot = Join-Path $RepositoryRoot '.local'
  $buildRoot = Join-Path $localRoot 'engine-preview'
  [pscustomobject]@{
    RepositoryRoot = $RepositoryRoot
    LocalRoot = $localRoot
    BuildRoot = $buildRoot
    EngineWorkingDirectory = $buildRoot
    EnginePath = Join-Path $buildRoot 'Release/hibiki_engine_preview.exe'
    ScenarioDirectory = Join-Path $localRoot 'engine-preview-multi-soak'
    Rounds = $Rounds
  }
}

function Assert-EnginePreviewMultiSoakRounds {
  param([Parameter(Mandatory)][int]$Value)
  if ($Value -lt 1 -or $Value -gt 20) {
    throw "Concurrent soak round count is out of bounds: $Value"
  }
}

# The four scenarios mirror the maintainer's real playback mix. Each one is a
# distinct bounded Float32 WAV rendered by its own offline engine process; the
# concurrency claim comes from running all four in the same round, not from
# pretending one engine hosts four sessions. Source fixtures must stay within
# the engine's fixed 4096-frame IR kernel bound (kMaxRealtimeIrTapsV1); longer
# sources fail closed in the shared WAV decoder before rendering.
$script:MultiSoakMaxSourceFrames = 4096
$script:MultiSoakScenarios = @(
  [pscustomobject]@{
    Id = 'djmax-low-latency'
    Label = 'DJMAX-style low-latency game loop'
    Channels = 2; SampleRate = 48000; Seconds = 0.085
    FrequencyHz = 220.0; Amplitude = 0.7; DcBias = 0.0; Sweep = $false
    ExpectedFrames = 4080
  },
  [pscustomobject]@{
    Id = 'netflix-movie'
    Label = 'Netflix-style movie playback'
    Channels = 2; SampleRate = 44100; Seconds = 0.09
    FrequencyHz = 320.0; Amplitude = 0.4; DcBias = 0.012; Sweep = $true
    ExpectedFrames = 4320
  },
  [pscustomobject]@{
    Id = 'zzz-51-game'
    Label = 'ZZZ-style 5.1 surround game'
    Channels = 6; SampleRate = 48000; Seconds = 0.075
    FrequencyHz = 180.0; Amplitude = 0.5; DcBias = -0.01; Sweep = $false
    ExpectedFrames = 3600
  },
  [pscustomobject]@{
    Id = 'flstudio-chrome'
    Label = 'FL Studio ASIO plus Chrome tab audio'
    Channels = 2; SampleRate = 44100; Seconds = 0.06
    FrequencyHz = 900.0; Amplitude = 0.3; DcBias = 0.02; Sweep = $false
    ExpectedFrames = 2880
  }
)

function Write-MultiSoakScenarioWav {
  param(
    [Parameter(Mandatory)]$Scenario,
    [Parameter(Mandatory)][string]$Path
  )

  # The engine offline render contract accepts mono or stereo sources only, so
  # surround scenarios keep their real channel identity in metadata but are
  # generated as a bounded stereo downmix here; the render stays an honest
  # stereo pass instead of claiming the engine decoded six channels.
  $frameCount = [int]($Scenario.SampleRate * $Scenario.Seconds)
  if ($frameCount -lt 1) { throw "Scenario '$($Scenario.Id)' has no frames." }
  $dataBytes = [uint64]($frameCount * 2 * 4)
  if ($dataBytes -gt [uint32]::MaxValue) { throw "Scenario '$($Scenario.Id)' exceeds the WAV chunk bound." }
  # The generated file is always a stereo downmix regardless of the
  # scenario metadata channel identity; the fmt chunk must match.
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
                                   [System.IO.FileAccess]::Write,
                                   [System.IO.FileShare]::None)
  try {
    $writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::ASCII, $false)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + $dataBytes))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]3)
    $writer.Write([uint16]2)
    $writer.Write([uint32]$Scenario.SampleRate)
    $writer.Write([uint32]($Scenario.SampleRate * 2 * 4))
    $writer.Write([uint16](2 * 4))
    $writer.Write([uint16]32)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]$dataBytes)
    for ($frame = 0; $frame -lt $frameCount; $frame++) {
      $channelSum = [double]0
      for ($channel = 0; $channel -lt $Scenario.Channels; $channel++) {
        $phase = 2 * [Math]::PI * $Scenario.FrequencyHz * $frame / $Scenario.SampleRate
        $channelSum += [double]($Scenario.Amplitude * [Math]::Sin($phase + (0.35 * $channel)) +
                                $Scenario.DcBias)
      }
      $downmixed = [single]($channelSum / $Scenario.Channels)
    $writer.Write($downmixed)
    $writer.Write($downmixed)
    }
    $writer.Flush()
    $writer.Dispose()
  } finally {
    $stream.Dispose()
  }
}

function Get-MultiSoakScenarioExpectation {
  param([Parameter(Mandatory)]$Scenario)

  if ($Scenario.SampleRate -eq 48000) {
    return [pscustomobject]@{
      ExpectedFrames = $Scenario.ExpectedFrames
      Resampled = $false
      SourceSampleRate = 48000
      OutputSampleRate = 48000
    }
  }
  return [pscustomobject]@{
    ExpectedFrames = $Scenario.ExpectedFrames
    Resampled = $true
    SourceSampleRate = $Scenario.SampleRate
    OutputSampleRate = 48000
  }
}

function Read-EnginePreviewMultiSoakWavDataChunk {
  param([Parameter(Mandatory)][string]$Path)
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                   [System.IO.FileAccess]::Read,
                                   [System.IO.FileShare]::Read)
  try {
    $reader = [System.IO.BinaryReader]::new($stream)
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'RIFF') {
      throw "Rendered WAV is missing the RIFF header: $Path"
    }
    [void]$reader.ReadBytes(4)
    if ([System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'WAVE') {
      throw "Rendered WAV is missing the WAVE tag: $Path"
    }
    while ($reader.BaseStream.Position -lt ($reader.BaseStream.Length - 8)) {
      $chunkId = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
      $chunkSize = $reader.ReadUInt32()
      if ($chunkId -eq 'data') { return $reader.ReadBytes([int]$chunkSize) }
      [void]$reader.ReadBytes([int]$chunkSize)
    }
  throw "Rendered WAV has no data chunk: $Path"
  } finally {
    $reader.Dispose()
  }
}

function Convert-EnginePreviewMultiSoakBytesToFloat32 {
  param([Parameter(Mandatory)][byte[]]$Bytes)
  if (($Bytes.Length % 4) -ne 0) {
    throw "Float32 data chunk length must be a multiple of four: $($Bytes.Length)."
  }
  $floats = New-Object 'float[]' ([int]($Bytes.Length / 4))
  [Buffer]::BlockCopy($Bytes, 0, $floats, 0, $Bytes.Length)
  return $floats;
}

function Get-EnginePreviewMultiSoakWavStats {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][float[]]$Samples,
    [Parameter(Mandatory)][ValidateRange(1, 64)][int]$Channels
  )
  if (($Samples.Count % $Channels) -ne 0) {
    throw "Interleaved sample count must be divisible by the channel count: $($Samples.Count) / $Channels."
  }
  $peak = [double]0
  $sumSquared = [double]0
  $sum = [double]0
  foreach ($sample in $Samples) {
    $value = [double]$sample
    $absolute = [Math]::Abs($value)
    if ($absolute -gt $peak) { $peak = $absolute }
    $sumSquared += $value * $value
    $sum += $value
  }
  return [pscustomobject]@{
    Peak = $peak
    Rms = [Math]::Sqrt($sumSquared / $Samples.Count)
    DcOffset = $sum / $Samples.Count
    FrameCount = [int64]($Samples.Count / $Channels)
  }
}

function Assert-EnginePreviewMultiSoakScenarioResult {
  param(
    [Parameter(Mandatory)]$Scenario,
    [Parameter(Mandatory)]$Expectation,
    [Parameter(Mandatory)][int]$ExitCode,
    [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Output,
    [Parameter(Mandatory)][string]$RenderPath,
    [Parameter(Mandatory)][int]$Round
  )

  if ($ExitCode -ne 0) {
    throw ("Round {0} scenario '{1}' failed: exit={2} output={3}" -f $Round, $Scenario.Id, $ExitCode, ($Output -join ' | '))
  }
  $summary = ($Output | Out-String)
  if (-not $summary.Contains('offline render complete')) {
    throw "Round $Round scenario '$($Scenario.Id)' did not report a completed offline render."
  }
  if (-not (Test-Path -LiteralPath $RenderPath)) {
    throw "Round $Round scenario '$($Scenario.Id)' produced no WAV file."
  }
  $renderedBytes = Read-EnginePreviewMultiSoakWavDataChunk -Path $RenderPath
  $renderedSamples = Convert-EnginePreviewMultiSoakBytesToFloat32 -Bytes $renderedBytes
  $stats = Get-EnginePreviewMultiSoakWavStats -Samples $renderedSamples -Channels 2
  if ($stats.FrameCount -lt [int64]1) {
    throw "Round $Round scenario '$($Scenario.Id)' rendered no frames."
  }
  if (($stats.Peak -le [double]0) -or ($stats.Peak -ge [double]1.0)) {
    throw ("Round {0} scenario '{1}' peak is outside the safe window: {2}" -f $Round, $Scenario.Id, $stats.Peak)
  }
  if ($stats.Rms -le [double]0) {
    throw "Round $Round scenario '$($Scenario.Id)' rendered silence."
  }
  if ($Expectation.Resampled -and -not $summary.Contains('resampled')) {
    throw "Round $Round scenario '$($Scenario.Id)' omitted its expected resample conversion."
  }
  return $stats
}

function Get-EnginePreviewMultiSoakAggregate {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Results,
    [Parameter(Mandatory)][int]$RequestedRounds,
    [Parameter(Mandatory)][int]$ScenarioCount
  )

  $materialized = @($Results)
  $passed = @(@($materialized) | Where-Object { $_.result -eq 'pass' }).Count
  $failed = @(@($materialized) | Where-Object { $_.result -eq 'fail' }).Count
  $durations = @($materialized | ForEach-Object { [double]$_.duration_ms })
  $average = $null
  $maximum = $null
  if ($durations.Count -gt 0) {
    $summary = $durations | Measure-Object -Maximum -Average
    $average = [math]::Round($summary.Average, 1)
    $maximum = [math]::Round($summary.Maximum, 1)
  }
  return [pscustomobject]@{
    requested_rounds = $RequestedRounds
    scenario_count = $ScenarioCount
    completed_scenarios = $materialized.Count
    passed_scenarios = $passed
    failed_scenarios = $failed
    average_scenario_duration_ms = $average
    slowest_scenario_duration_ms = $maximum
  }
}

function Invoke-EnginePreviewMultiSoakSelfTest {
  $cases = 0
  Assert-EnginePreviewMultiSoakRounds -Value 1; $cases++
  Assert-EnginePreviewMultiSoakRounds -Value 20; $cases++

  $lowCaught = $false
  try { Assert-EnginePreviewMultiSoakRounds -Value 0 } catch { $lowCaught = $_.Exception.Message -match 'out of bounds' }
  if (-not $lowCaught) { throw 'multi-soak self-test expected a zero-round rejection.' }
  $cases++

  $highCaught = $false
  try { Assert-EnginePreviewMultiSoakRounds -Value 21 } catch { $highCaught = $_.Exception.Message -match 'out of bounds' }
  if (-not $highCaught) { throw 'multi-soak self-test expected an over-round rejection.' }
  $cases++

  if ($script:MultiSoakScenarios.Count -ne 4) {
    throw 'multi-soak self-test requires exactly four scenarios.'
  }
  $cases++

  $seenIds = @{}
  foreach ($scenario in $script:MultiSoakScenarios) {
    if ([string]::IsNullOrWhiteSpace($scenario.Id)) { throw 'multi-soak scenario id is blank.' }
    if ($seenIds.ContainsKey($scenario.Id)) { throw "multi-soak scenario id is duplicated: $($scenario.Id)" }
    $seenIds[$scenario.Id] = $true
    if ($scenario.Channels -ne 2 -and $scenario.Channels -ne 6) {
      throw "multi-soak scenario has an unexpected channel count: $($scenario.Id)"
    }
  }
  $cases++
  # The offline engine contract is mono/stereo in and bounded Float32 PCM;
  # the harness must keep fixtures inside the decoder envelope and the fixed
  # 4096-frame IR kernel bound before dispatch.
  foreach ($scenario in $script:MultiSoakScenarios) {
    if ($scenario.SampleRate -lt 8000 -or $scenario.SampleRate -gt 192000) {
      throw "multi-soak scenario sample rate is outside the decoder bound: $($scenario.Id)"
    }
    $sourceFrames = [int]($scenario.SampleRate * $scenario.Seconds)
    if ($sourceFrames -lt 1 -or $sourceFrames -gt $script:MultiSoakMaxSourceFrames) {
      throw ("multi-soak scenario source exceeds the engine 4096-frame IR kernel bound: {0} ({1} frames)" -f $scenario.Id, $sourceFrames)
    }
  }
  $cases++

  $stereo = Get-MultiSoakScenarioExpectation -Scenario ($script:MultiSoakScenarios | Where-Object Id -eq 'djmax-low-latency')
  if ($stereo.Resampled) { throw 'multi-soak expectation wrongly resamples a 48 kHz source.' }
  $cases++

  $resampled = Get-MultiSoakScenarioExpectation -Scenario ($script:MultiSoakScenarios | Where-Object Id -eq 'netflix-movie')
  if (-not $resampled.Resampled -or $resampled.ExpectedFrames -ne 4320) {
    throw 'multi-soak expectation is wrong for the 44.1 kHz scenario.'
  }
  $cases++

  return $cases
}

if ($SelfTest) {
  $cases = Invoke-EnginePreviewMultiSoakSelfTest
  Write-Output ("Engine Preview multi-scenario soak self-test passed ({0} cases; offline/no-process/no-file-write)." -f $cases)
  exit 0
}

$soakPlan = Get-EnginePreviewMultiSoakPlan -RepositoryRoot $repo -Rounds $Rounds
if (-not (Test-Path -LiteralPath $soakPlan.EnginePath)) {
  throw "Engine Preview build not found; run tools/build-engine-preview.ps1 first: $($soakPlan.EnginePath)"
}
New-Item -ItemType Directory -Force -Path $soakPlan.ScenarioDirectory | Out-Null

$workerScript = Join-Path $PSScriptRoot 'engine-preview-multi-soak-worker.ps1'
$runStartedAt = (Get-Date).ToUniversalTime()
$results = @()
for ($round = 1; $round -le $Rounds; $round++) {
  $roundJobs = @()
foreach ($scenario in $script:MultiSoakScenarios) {
    $sourcePath = Join-Path $soakPlan.ScenarioDirectory ("{0}-source.wav" -f $scenario.Id)
    $renderPath = Join-Path $soakPlan.ScenarioDirectory ("{0}-render.wav" -f $scenario.Id)
    $stdoutPath = Join-Path $soakPlan.ScenarioDirectory ("{0}-round{1}-stdout.txt" -f $scenario.Id, $round)
    $stderrPath = Join-Path $soakPlan.ScenarioDirectory ("{0}-round{1}-stderr.txt" -f $scenario.Id, $round)
    Write-MultiSoakScenarioWav -Scenario $scenario -Path $sourcePath
    $process = Start-Process -FilePath 'pwsh' -ArgumentList @(
      '-NoProfile', '-File', ('"{0}"' -f $workerScript),
      '-EnginePath', ('"{0}"' -f $soakPlan.EnginePath),
      '-WorkingDirectory', ('"{0}"' -f $soakPlan.EngineWorkingDirectory),
      '-SourcePath', ('"{0}"' -f $sourcePath),
      '-RenderPath', ('"{0}"' -f $renderPath),
      '-StdoutCapturePath', ('"{0}"' -f $stdoutPath),
      '-StderrCapturePath', ('"{0}"' -f $stderrPath)
    ) -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    $roundJobs += [pscustomobject]@{
      Scenario = $scenario
      RenderPath = $renderPath
      StdoutCapturePath = $stdoutPath
      Process = $process
      StartedAt = (Get-Date).ToUniversalTime()
    }
  }

  foreach ($job in $roundJobs) {
    if (-not $job.Process.WaitForExit(30000)) {
      try { Stop-Process -Id $job.Process.Id -Force -ErrorAction SilentlyContinue } catch { }
      $results += [pscustomobject]@{ round = $round; scenario = $job.Scenario.Id; result = 'fail'; duration_ms = [math]::Round(((Get-Date).ToUniversalTime() - $job.StartedAt).TotalMilliseconds, 1); exit_code = $null; frames = $null; peak = $null; rms = $null; dc_offset = $null }
      continue
    }
    try {
      $engineOutput = @(Get-Content -LiteralPath $job.StdoutCapturePath -ErrorAction SilentlyContinue)
      $stats = Assert-EnginePreviewMultiSoakScenarioResult -Scenario $job.Scenario `
        -Expectation (Get-MultiSoakScenarioExpectation -Scenario $job.Scenario) `
        -RenderPath $job.RenderPath -ExitCode $job.Process.ExitCode -Round $round `
        -Output ([string[]]$engineOutput)
      $results += [pscustomobject]@{ round = $round; scenario = $job.Scenario.Id; result = 'pass'; duration_ms = [math]::Round(((Get-Date).ToUniversalTime() - $job.StartedAt).TotalMilliseconds, 1); exit_code = $job.Process.ExitCode; frames = $stats.FrameCount; peak = [math]::Round($stats.Peak, 6); rms = [math]::Round($stats.Rms, 6); dc_offset = [math]::Round([Math]::Abs($stats.DcOffset), 6) }
    } catch {
      Write-Warning $_.Exception.Message
      $results += [pscustomobject]@{ round = $round; scenario = $job.Scenario.Id; result = 'fail'; duration_ms = [math]::Round(((Get-Date).ToUniversalTime() - $job.StartedAt).TotalMilliseconds, 1); exit_code = $job.Process.ExitCode; frames = $null; peak = $null; rms = $null; dc_offset = $null }
    }
  }
}

$aggregate = Get-EnginePreviewMultiSoakAggregate -Results $results -RequestedRounds $Rounds -ScenarioCount $script:MultiSoakScenarios.Count
$finishedAt = (Get-Date).ToUniversalTime()
$report = [ordered]@{
  schema_version = 1
  harness = 'engine-preview-multi-soak'
  requested_rounds = $aggregate.requested_rounds
  scenario_count = $aggregate.scenario_count
  completed_scenarios = $aggregate.completed_scenarios
  passed_scenarios = $aggregate.passed_scenarios
  failed_scenarios = $aggregate.failed_scenarios
  average_scenario_duration_ms = $aggregate.average_scenario_duration_ms
  slowest_scenario_duration_ms = $aggregate.slowest_scenario_duration_ms
  started_at = $runStartedAt.ToString('o')
  finished_at = $finishedAt.ToString('o')
  results = @(
    $results | ForEach-Object {
      [ordered]@{ round = $_.round; scenario = $_.scenario; result = $_.result; duration_ms = $_.duration_ms; exit_code = $_.exit_code; frames = $_.frames; peak = $_.peak; rms = $_.rms; dc_offset = $_.dc_offset }
    }
  )
}
$reportPath = Join-Path $soakPlan.ScenarioDirectory 'report.json'
[System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
Write-Output (
  'Engine Preview multi-scenario soak finished: passed=' + $aggregate.passed_scenarios +
  ' failed=' + $aggregate.failed_scenarios + ' slowest=' + $aggregate.slowest_scenario_duration_ms +
  ' ms; anonymous report under .local.'
)
if ($aggregate.failed_scenarios -gt 0) { exit 1 }
