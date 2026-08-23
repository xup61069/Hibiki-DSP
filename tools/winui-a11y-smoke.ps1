#Requires -Version 7
[CmdletBinding()]
param(
  [switch]$SelfTest,
  [string]$OutputDir
)

Set-StrictMode -Version Latest

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

function Test-A11yPathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Get-A11yExistingAttributes {
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
    throw "WinUI accessibility path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "WinUI accessibility path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-A11ySafePath {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][ValidateSet('Directory', 'File')][string]$Kind,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $expectedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (-not (Test-A11yPathUnderRoot -Path $candidate -Root $expectedRoot)) {
    throw "WinUI accessibility path must remain under the repository .local root: $candidate"
  }

  $cursor = $candidate
  while ($true) {
    $attributes = Get-A11yExistingAttributes -Path $cursor `
      -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -eq $attributes -and $cursor -eq $candidate) {
      throw "WinUI accessibility $Kind does not exist: $candidate"
    }
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "WinUI accessibility path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $candidate) {
        if ($Kind -eq 'Directory' -and ($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "WinUI accessibility output is not a directory: $candidate"
        }
        if ($Kind -eq 'File' -and ($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
          throw "WinUI accessibility executable is a directory: $candidate"
        }
      }
    }

    if ($cursor -eq $expectedRoot) { break }
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "WinUI accessibility path could not reach the repository .local root: $candidate"
    }
    $cursor = $parent
  }
}

function Assert-A11yOutputDirectory {
  param(
    [Parameter(Mandatory)][string]$OutputDir,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  Assert-A11ySafePath -Path $OutputDir -Root (Join-Path $RepositoryRoot '.local') `
    -Kind Directory -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
}

function Assert-A11yLaunchTarget {
  param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputDir,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  if (-not (Test-A11yPathUnderRoot -Path $Executable -Root $OutputDir)) {
    throw "WinUI accessibility executable must remain under the selected output directory: $Executable"
  }
  Assert-A11ySafePath -Path $Executable -Root (Join-Path $RepositoryRoot '.local') `
    -Kind File -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
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
  $reasons = @(Test-ControlExpectations -Inventory $good)
  if ($reasons.Count -ne 0) { throw "winui a11y self-test failed: happy-path inventory rejected: $($reasons -join '; ')" }
  $caseCount++

  # Empty inventory must be rejected.
  $reasons = @(Test-ControlExpectations -Inventory @())
  if ($reasons.Count -eq 0) { throw 'winui a11y self-test failed: empty inventory accepted.' }
  $caseCount++

  # Below-minimum interactive controls must be rejected.
  $sparse = @(@{ type = 'Text'; id = ''; name = 'label' }, @{ type = 'Group'; id = 'g'; name = 'root' })
  $reasons = @(Test-ControlExpectations -Inventory $sparse)
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

  $localRoot = [IO.Path]::GetFullPath((Join-Path $repo '.local')).TrimEnd('\', '/')
  $syntheticOutput = [IO.Path]::GetFullPath((Join-Path $repo '.local/a11y-selftest')).TrimEnd('\', '/')
  $syntheticExecutable = [IO.Path]::GetFullPath((Join-Path $repo '.local/a11y-selftest/Hibiki.WinUI.exe')).TrimEnd('\', '/')
  $validAttributes = @{
    $localRoot = [System.IO.FileAttributes]::Directory
    $syntheticOutput = [System.IO.FileAttributes]::Directory
    $syntheticExecutable = [System.IO.FileAttributes]::Archive
  }
  Assert-A11yOutputDirectory -OutputDir $syntheticOutput -RepositoryRoot $repo -SyntheticAttributes $validAttributes
  Assert-A11yLaunchTarget -Executable $syntheticExecutable -OutputDir $syntheticOutput `
    -RepositoryRoot $repo -SyntheticAttributes $validAttributes
  $caseCount++

  $outsideCaught = $false
  try { Assert-A11yOutputDirectory -OutputDir (Join-Path $repo 'build') -RepositoryRoot $repo } catch { $outsideCaught = $true }
  if (-not $outsideCaught) { throw 'winui a11y self-test expected an outside-root output rejection.' }
  $caseCount++

  $reparseParentCaught = $false
  try {
    Assert-A11yOutputDirectory -OutputDir $syntheticOutput -RepositoryRoot $repo `
      -SyntheticAttributes @{
        $localRoot = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint
        $syntheticOutput = [System.IO.FileAttributes]::Directory
      }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseParentCaught) { throw 'winui a11y self-test expected a reparse-parent rejection.' }
  $caseCount++

  $reparseTargetCaught = $false
  try {
    Assert-A11yOutputDirectory -OutputDir $syntheticOutput -RepositoryRoot $repo `
      -SyntheticAttributes @{
        $localRoot = [System.IO.FileAttributes]::Directory
        $syntheticOutput = [System.IO.FileAttributes]::Directory -bor [System.IO.FileAttributes]::ReparsePoint
      }
  } catch { $reparseTargetCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseTargetCaught) { throw 'winui a11y self-test expected a reparse-target rejection.' }
  $caseCount++

  $nonDirectoryCaught = $false
  try {
    Assert-A11yOutputDirectory -OutputDir $syntheticOutput -RepositoryRoot $repo `
      -SyntheticAttributes @{
        $localRoot = [System.IO.FileAttributes]::Directory
        $syntheticOutput = [System.IO.FileAttributes]::Archive
      }
  } catch { $nonDirectoryCaught = $_.Exception.Message -match 'not a directory' }
  if (-not $nonDirectoryCaught) { throw 'winui a11y self-test expected a non-directory rejection.' }
  $caseCount++

  $outsideExecutableCaught = $false
  $outsideExecutable = [IO.Path]::GetFullPath((Join-Path $repo '.local/other/Hibiki.WinUI.exe')).TrimEnd('\', '/')
  try {
    Assert-A11yLaunchTarget -Executable $outsideExecutable -OutputDir $syntheticOutput `
      -RepositoryRoot $repo -SyntheticAttributes $validAttributes
  } catch { $outsideExecutableCaught = $_.Exception.Message -match 'selected output directory' }
  if (-not $outsideExecutableCaught) { throw 'winui a11y self-test expected an executable-outside-output rejection.' }
  $caseCount++

  $reparseExecutableCaught = $false
  try {
    Assert-A11yLaunchTarget -Executable $syntheticExecutable -OutputDir $syntheticOutput `
      -RepositoryRoot $repo -SyntheticAttributes @{
        $localRoot = [System.IO.FileAttributes]::Directory
        $syntheticOutput = [System.IO.FileAttributes]::Directory
        $syntheticExecutable = [System.IO.FileAttributes]::ReparsePoint
      }
  } catch { $reparseExecutableCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseExecutableCaught) { throw 'winui a11y self-test expected a reparse-executable rejection.' }
  $caseCount++

  $leafInspectionErrorCaught = $false
  try {
    Assert-A11yLaunchTarget -Executable $syntheticExecutable -OutputDir $syntheticOutput `
      -RepositoryRoot $repo -SyntheticAttributes @{ $localRoot = [System.IO.FileAttributes]::Directory; $syntheticOutput = [System.IO.FileAttributes]::Directory } `
      -SyntheticInspectionErrors @{ $syntheticExecutable = 'synthetic access denied' }
  } catch { $leafInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $leafInspectionErrorCaught) { throw 'winui a11y self-test expected a leaf inspection-error rejection.' }
  $caseCount++

  $parentInspectionErrorCaught = $false
  try {
    Assert-A11yLaunchTarget -Executable $syntheticExecutable -OutputDir $syntheticOutput `
      -RepositoryRoot $repo -SyntheticAttributes @{ $syntheticOutput = [System.IO.FileAttributes]::Directory; $syntheticExecutable = [System.IO.FileAttributes]::Archive } `
      -SyntheticInspectionErrors @{ $localRoot = 'synthetic sharing violation' }
  } catch { $parentInspectionErrorCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $parentInspectionErrorCaught) { throw 'winui a11y self-test expected a parent inspection-error rejection.' }
  $caseCount++

  if ($caseCount -lt 13) { throw "winui a11y self-test failed: expected at least 13 passing cases, saw $caseCount." }
  Write-Output "WinUI accessibility smoke self-test passed ($caseCount cases; expectation thresholds, empty/sparse rejection, evidence JSON shape, inspection errors)."
  exit 0
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  throw 'Usage: tools/winui-a11y-smoke.ps1 [-OutputDir <formal-build-dir>] or -SelfTest.'
}

Assert-A11yOutputDirectory -OutputDir $OutputDir -RepositoryRoot $repo
$exe = Join-Path $OutputDir 'Hibiki.WinUI.exe'
Assert-A11yLaunchTarget -Executable $exe -OutputDir $OutputDir -RepositoryRoot $repo

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$clock = [Diagnostics.Stopwatch]::StartNew()
Assert-A11yOutputDirectory -OutputDir $OutputDir -RepositoryRoot $repo
Assert-A11yLaunchTarget -Executable $exe -OutputDir $OutputDir -RepositoryRoot $repo
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

  $reasons = @(Test-ControlExpectations -Inventory $inventory)
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
