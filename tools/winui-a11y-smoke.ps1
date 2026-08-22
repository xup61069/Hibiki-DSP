[CmdletBinding()]
param(
  [switch]$SelfTest,
  [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Pure expectation check over a plain hashtable inventory so the offline
# self-test can exercise it without launching anything or loading UIA.
function Test-ControlExpectations {
  param([Parameter(Mandatory)][AllowEmptyCollection()][hashtable[]]$Inventory)
  $reasons = @()
  if ($Inventory.Count -lt 10) { $reasons += 'fewer than 10 observable controls' }
  $namedGroups = @($Inventory | Where-Object { $_.type -eq 'Group' -and $_.name })
  if ($namedGroups.Count -lt 1) { $reasons += 'no named Group container' }
  $combos = @($Inventory | Where-Object { $_.type -eq 'ComboBox' })
  if ($combos.Count -lt 2) { $reasons += 'fewer than 2 ComboBox controls' }
  $buttons = @($Inventory | Where-Object { $_.type -eq 'Button' })
  if ($buttons.Count -lt 4) { $reasons += 'fewer than 4 Button controls' }
  return $reasons
}

function New-EvidenceObject {
  param(
    [Parameter(Mandatory)][AllowEmptyCollection()][hashtable[]]$Inventory,
    [Parameter(Mandatory)][string]$BuildTool,
    [Parameter(Mandatory)][int]$ElapsedMs
  )
  $types = @{}
  foreach ($entry in $Inventory) {
    $t = [string]$entry.type
    if ($types.ContainsKey($t)) { $types[$t] = $types[$t] + 1 } else { $types[$t] = 1 }
  }
  $ev = [ordered]@{
    schema_version   = 1
    tool             = 'winui-a11y-smoke'
    build_tool       = $BuildTool
    elapsed_ms       = $ElapsedMs
    control_count    = $Inventory.Count
    control_types    = $types
    controls         = @($Inventory | ForEach-Object { [pscustomobject]@{ type = [string]$_.type; automation_id = [string]$_.id; name = [string]$_.name } })
    expectations     = 'at-least: 10 controls, 1 named Group, 2 ComboBox, 4 Button'
  }
  # Return as pscustomobject so callers/JSON never enumerate-unwrap the map.
  return [pscustomobject]$ev
}

if ($SelfTest) {
  $caseCount = 0

  # Happy path: synthetic inventory mirrors the observed live tree shape.
  $good = @(
    @{ type = 'Pane'; id = ''; name = '' },
    @{ type = 'Group'; id = 'RootGrid'; name = 'Hibiki DSP' },
    @{ type = 'Text'; id = ''; name = 'Hibiki DSP' },
    @{ type = 'Button'; id = ''; name = 'connect-engine' },
    @{ type = 'ComboBox'; id = 'OutputGroup'; name = 'output-group' },
    @{ type = 'ComboBox'; id = 'OutputDevice'; name = 'output-device' },
    @{ type = 'Button'; id = ''; name = 'select-output-device' },
    @{ type = 'Button'; id = ''; name = 'scene-game' },
    @{ type = 'Button'; id = ''; name = 'scene-movie' },
    @{ type = 'Button'; id = ''; name = 'volume' }
  )
  $reasons = Test-ControlExpectations -Inventory $good
  if ($reasons.Count -ne 0) { throw "winui a11y self-test failed: happy-path inventory rejected: $($reasons -join '; ')" }
  $caseCount++

  # Empty inventory must be rejected.
  $reasons = Test-ControlExpectations -Inventory @()
  if ($reasons.Count -eq 0) { throw 'winui a11y self-test failed: empty inventory accepted.' }
  $caseCount++

  # Below-minimum interactive controls must be rejected.
  $sparse = @(@{ type = 'Text'; id = ''; name = 'label' }, @{ type = 'Group'; id = 'g'; name = 'root' })
  $reasons = Test-ControlExpectations -Inventory $sparse
  if ($reasons.Count -eq 0) { throw 'winui a11y self-test failed: sparse inventory accepted.' }
  $caseCount++

  # Evidence object must serialize with stable top-level keys and no identity leaks.
  $ev = New-EvidenceObject -Inventory $good -BuildTool 'selftest' -ElapsedMs 7
  $json = ConvertTo-Json -InputObject $ev -Depth 4
  foreach ($key in @('schema_version', 'tool', 'control_count', 'control_types', 'controls', 'expectations')) {
    $needle = '"' + $key + '"'
    if (-not $json.Contains($needle)) { throw "winui a11y self-test failed: evidence JSON missing key $key. json-preview=[" + $json.Substring(0, [Math]::Min(220, $json.Length)) + "]" }
  }
  foreach ($forbidden in @('hostname', 'user', 'C:\\')) {
    if ($json.ToLowerInvariant().Contains($forbidden.ToLowerInvariant())) { throw "winui a11y self-test failed: evidence JSON leaked forbidden token $forbidden." }
  }
  $caseCount++

  if ($caseCount -lt 4) { throw "winui a11y self-test failed: expected at least 4 passing cases, saw $caseCount." }
  Write-Output "WinUI accessibility smoke self-test passed ($caseCount cases; expectation thresholds, empty/sparse rejection, evidence JSON shape)."
  exit 0
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  throw 'Usage: tools/winui-a11y-smoke.ps1 [-OutputDir <formal-build-dir>] or -SelfTest.'
}

$exe = Join-Path $OutputDir 'Hibiki.WinUI.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Hibiki.WinUI.exe not found under '$OutputDir'." }

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$clock = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $exe -WorkingDirectory $OutputDir -PassThru
try {
  $root = [System.Windows.Automation.AutomationElement]::RootElement
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $proc.Id)
  $deadline = (Get-Date).AddSeconds(20)
  $window = $null
  while ((Get-Date) -lt $deadline -and -not $window) {
    $window = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
    if (-not $window) { Start-Sleep -Milliseconds 250 }
  }
  if (-not $window -or $proc.HasExited) {
    throw 'WinUI window did not become available through UI Automation within the bounded wait.'
  }

  $all = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.Condition]::TrueCondition)
  $inventory = @()
  $cap = [Math]::Min(500, [Math]::Max(1, $all.Count))
  for ($i = 0; $i -lt $cap; $i++) {
    $el = $all.Item($i)
    $inventory += @{
      type = [string]$el.Current.ControlType.ProgrammaticName.Replace('ControlType.', '')
      id   = [string]$el.Current.AutomationId
      name = [string]$el.Current.Name
    }
  }

  $reasons = Test-ControlExpectations -Inventory $inventory
  if ($reasons.Count -gt 0) {
    throw ("WinUI accessibility expectations unmet: " + ($reasons -join '; '))
  }

  $evidence = New-EvidenceObject -Inventory $inventory -BuildTool 'visual-studio-msbuild' -ElapsedMs $clock.ElapsedMilliseconds
  $outDir = Join-Path $repo '.local'
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
  $outPath = Join-Path $outDir 'winui-a11y-smoke-v1.json'
  $evidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $outPath -Encoding UTF8NoBOM

  Write-Output ("WinUI accessibility smoke passed: $($inventory.Count) observable controls recorded to $outPath.")
}
finally {
  if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  }
}
