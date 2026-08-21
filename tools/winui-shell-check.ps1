[CmdletBinding()]
param(
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$interactiveControlPattern = '(?ms)<(?<type>Button|ComboBox|Slider|ToggleSwitch|CheckBox|TextBox|NumberBox)\b(?<attributes>[^>]*?)(?:/?>)'

function Get-InteractiveControlOpenings([string]$xaml) {
  foreach ($match in [regex]::Matches($xaml, $script:interactiveControlPattern)) {
    $line = 1 + @($xaml.Substring(0, $match.Index) -split "`n").Count - 1
    [pscustomobject]@{
      Type = $match.Groups['type'].Value
      Attributes = $match.Groups['attributes'].Value
      Line = $line
    }
  }
}

function Assert-InteractiveControlNames([string]$xaml, [string]$sourceName) {
  $controls = @(Get-InteractiveControlOpenings $xaml)
  $missing = @($controls | Where-Object {
      $doubleQuoted = [regex]::Match($_.Attributes, 'AutomationProperties\.Name\s*=\s*"(?<value>[^"]*)"')
      $singleQuoted = [regex]::Match($_.Attributes, "AutomationProperties\.Name\s*=\s*'(?<value>[^']*)'")
      (!$doubleQuoted.Success -and !$singleQuoted.Success) -or
        (($doubleQuoted.Success -and [string]::IsNullOrWhiteSpace($doubleQuoted.Groups['value'].Value)) -or
        ($singleQuoted.Success -and [string]::IsNullOrWhiteSpace($singleQuoted.Groups['value'].Value)))
    })
  if ($missing.Count -gt 0) {
    $details = @($missing | ForEach-Object { "$($_.Type) at $($sourceName):$($_.Line)" }) -join ', '
    throw "Interactive WinUI controls must declare a non-empty AutomationProperties.Name: $details"
  }
  return $controls.Count
}

if ($SelfTest) {
  $valid = @'
<StackPanel>
  <TextBlock Text="Not interactive" />
  <Button AutomationProperties.Name="Connect" />
  <ComboBox
      Header="Output"
      AutomationProperties.Name="Output" />
</StackPanel>
'@
  if ((Assert-InteractiveControlNames $valid 'selftest-valid.xaml') -ne 2) {
    throw 'WinUI accessibility self-test expected two interactive controls.'
  }

  $missing = '<Slider Minimum="0" Maximum="1" />'
  $missingCaught = $false
  try {
    [void](Assert-InteractiveControlNames $missing 'selftest-missing.xaml')
  } catch {
    $missingCaught = $true
    if ($_.Exception.Message -notmatch 'Slider at selftest-missing\.xaml:1') { throw }
  }
  if (-not $missingCaught) { throw 'WinUI accessibility self-test expected a missing-name failure.' }

  $empty = '<TextBox AutomationProperties.Name="" />'
  $emptyCaught = $false
  try {
    [void](Assert-InteractiveControlNames $empty 'selftest-empty.xaml')
  } catch {
    $emptyCaught = $true
    if ($_.Exception.Message -notmatch 'TextBox at selftest-empty\.xaml:1') { throw }
  }
  if (-not $emptyCaught) { throw 'WinUI accessibility self-test expected an empty-name failure.' }

  $unrelated = '<StackPanel><TextBlock Text="No automation name required" /></StackPanel>'
  if ((Assert-InteractiveControlNames $unrelated 'selftest-unrelated.xaml') -ne 0) {
    throw 'WinUI accessibility self-test counted a non-interactive element.'
  }
  Write-Output 'WinUI interactive-control accessibility self-test passed (4 cases).'
  exit 0
}

$repo = Split-Path -Parent $PSScriptRoot
$shell = Join-Path $repo 'apps/winui-shell'
$required = @('Hibiki.WinUI.csproj', 'App.xaml', 'App.xaml.cs', 'MainWindow.xaml', 'MainWindow.xaml.cs')
$missing = @($required | Where-Object { -not (Test-Path (Join-Path $shell $_)) })
if ($missing.Count -gt 0) { throw "WinUI shell files missing: $($missing -join ', ')" }

$project = Get-Content (Join-Path $shell 'Hibiki.WinUI.csproj') -Raw
$lock = Get-Content (Join-Path $repo 'build/toolchain-lock.yml') -Raw
if (-not $project.Contains('<UseWinUI>true</UseWinUI>') -or
    -not $project.Contains('<WindowsPackageType>None</WindowsPackageType>') -or
    -not $project.Contains('net8.0-windows10.0.26100.0') -or
    -not $project.Contains('<Platforms>x64</Platforms>')) {
  throw 'WinUI project must target WinUI/x64/Windows 11 build 26100.'
}
$projectVersion = [regex]::Match($project, 'WindowsAppSDKVersion[^>]*>([^<]+)<').Groups[1].Value
$lockVersion = [regex]::Match($lock, 'app_sdk:\s*"([^"]+)"').Groups[1].Value
if ([string]::IsNullOrWhiteSpace($projectVersion) -or $projectVersion -ne $lockVersion) {
  throw "WinUI App SDK version $projectVersion does not match toolchain lock $lockVersion."
}

$xaml = Get-Content (Join-Path $shell 'MainWindow.xaml') -Raw
$codeBehind = Get-Content (Join-Path $shell 'MainWindow.xaml.cs') -Raw
Write-Output "WinUI interactive-control accessibility scan: $(Assert-InteractiveControlNames $xaml 'apps/winui-shell/MainWindow.xaml') controls."
foreach ($requiredText in @('x:Name="RootGrid"', 'ItemsSource="{Binding Scenes}"',
    'ItemsSource="{Binding OutputGroups}"', 'SelectedOutputGroup', 'IsExpert',
    'ItemsSource="{Binding PhysicalDevices}"', 'SelectedPhysicalDeviceId',
    'DeviceSwitchStatusText', 'OnSwitchDeviceClick',
    'RequestedVolumeDb', 'Muted', 'IrAddedDelayMs', 'x:Name="ExpertPanel"',
    'EffectiveVolumeDb', 'SafetyStatusText', 'VolumeOriginText', 'VolumeActuatorText',
    'ItemsSource="{Binding Expert.RouteHealth}"', 'Expert.RouteSummary',
    'ItemsSource="{Binding Expert.MatrixRoutes}"', 'ItemsSource="{Binding Expert.DspGraph}"',
    'ItemsSource="{Binding Expert.Vst3Lanes}"', 'Expert.Calibration.Mode',
    'AutomationProperties.Name="Hibiki DSP 音訊場景控制"',
    'AutomationProperties.Name="一鍵改善聲音"',
    'AutomationProperties.Name="系統音量"',
    'AutomationProperties.Name="Expert 詳細模式"',
    'AutomationProperties.LiveSetting="Polite"', 'CustomSceneId', 'CustomSceneName',
    'CustomSceneDescription', 'OnAddCustomSceneClick')) {
  if (-not $xaml.Contains($requiredText)) { throw "WinUI shell binding missing: $requiredText" }
}
if (-not $codeBehind.Contains('RootGrid.DataContext = ViewModel') -or
    -not $codeBehind.Contains('ViewModel.ConnectAsync') -or
    -not $codeBehind.Contains('ViewModel.DisconnectAsync')) {
  throw 'WinUI shell must bind the control model and close its pipe client.'
}

$forbidden = @('AudioEngine', 'IAudioClient', 'Wasapi', 'CreateThread', 'NamedPipeServer')
foreach ($token in $forbidden) {
  if ($xaml.Contains($token) -or $codeBehind.Contains($token)) {
    throw "WinUI shell must not own audio/driver work: $token"
  }
}

Write-Output "WinUI source shell checks passed (Windows App SDK $lockVersion)."
