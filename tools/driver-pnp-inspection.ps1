[CmdletBinding()]
param(
  [string]$OutputPath,
  [ValidateRange(16, 512)][int]$SetupApiMaxLines = 160,
  [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$localRoot = Join-Path $repo '.local'
$targetInstancePattern = '(?i)^ROOT\\HIBIKIDSP(?:\\|$)'
$targetHardwareId = 'ROOT\HIBIKIDSP'
$targetService = 'HibikiVirtualAudio'

function Test-PathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][string]$Candidate
  )

  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate)
  $resolvedCandidate.StartsWith(
    $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase
  )
}

function Get-InspectionPathAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes
  )

  $resolvedPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($resolvedPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$resolvedPath]
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $resolvedPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Driver PnP inspection path check failed: $resolvedPath ($($_.Exception.Message))"
  }
}

function Assert-InspectionOutputPath {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Root,
    [hashtable]$SyntheticAttributes
  )

  $resolvedPath = [System.IO.Path]::GetFullPath($Path)
  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  if (-not (Test-PathUnderRoot -Root $resolvedRoot -Candidate $resolvedPath)) {
    throw "Driver PnP inspection output must remain under repository .local: $resolvedPath"
  }

  $cursor = Split-Path -Parent $resolvedPath
  while (-not [string]::IsNullOrWhiteSpace($cursor)) {
    $resolvedCursor = [System.IO.Path]::GetFullPath($cursor).TrimEnd('\', '/')
    $attributes = Get-InspectionPathAttributes -Path $resolvedCursor -SyntheticAttributes $SyntheticAttributes
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Driver PnP inspection output parent is a reparse point: $resolvedCursor"
      }
      if (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Driver PnP inspection output parent is not a directory: $resolvedCursor"
      }
    }
    if ($resolvedCursor -eq $resolvedRoot) { break }
    $parent = Split-Path -Parent $resolvedCursor
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $resolvedCursor) {
      throw "Driver PnP inspection output could not reach repository .local: $resolvedPath"
    }
    $cursor = $parent
  }

  $leafAttributes = Get-InspectionPathAttributes -Path $resolvedPath -SyntheticAttributes $SyntheticAttributes
  if ($null -ne $leafAttributes) {
    if (($leafAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "Driver PnP inspection output is a reparse point: $resolvedPath"
    }
    if (($leafAttributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
      throw "Driver PnP inspection output is a directory: $resolvedPath"
    }
  }
}

function Get-Sha256Token {
  param([Parameter(Mandatory)][string]$Value)

  $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
  $hash = [System.Security.Cryptography.SHA256]::HashData($bytes)
  ([System.Convert]::ToHexString($hash).ToLowerInvariant()).Substring(0, 16)
}

function ConvertTo-ProblemStatusHex {
  param($Value)

  if ($null -eq $Value) { return $null }
  if ($Value -is [string] -and $Value -match '^0x(?<hex>[0-9a-fA-F]{1,8})$') {
    return '0x' + $Matches.hex.PadLeft(8, '0').ToUpperInvariant()
  }
  try {
    $signed = [System.Convert]::ToInt64($Value, [System.Globalization.CultureInfo]::InvariantCulture)
    return ('0x{0:X8}' -f ($signed -band 0xFFFFFFFFL))
  }
  catch {
    return $null
  }
}

function Get-PropertyMap {
  param([object[]]$Properties)

  $map = @{}
  foreach ($property in @($Properties)) {
    $key = [string]$property.KeyName
    if (-not [string]::IsNullOrWhiteSpace($key)) { $map[$key] = $property.Data }
  }
  $map
}

function Test-HibikiTargetDevice {
  param(
    [Parameter(Mandatory)][string]$InstanceId,
    [hashtable]$PropertyMap
  )

  if ($InstanceId -match $targetInstancePattern) { return $true }
  if ($null -eq $PropertyMap -or -not $PropertyMap.ContainsKey('DEVPKEY_Device_HardwareIds')) {
    return $false
  }
  foreach ($hardwareId in @($PropertyMap['DEVPKEY_Device_HardwareIds'])) {
    if ([string]$hardwareId -eq $targetHardwareId) { return $true }
  }
  $false
}

function ConvertTo-HibikiDeviceSummary {
  param(
    [Parameter(Mandatory)]$Device,
    [object[]]$Properties
  )

  $propertyMap = Get-PropertyMap -Properties $Properties
  $instanceId = [string]$Device.InstanceId
  if (-not (Test-HibikiTargetDevice -InstanceId $instanceId -PropertyMap $propertyMap)) { return $null }
  $problemCode = $null
  if ($propertyMap.ContainsKey('DEVPKEY_Device_ProblemCode')) {
    try { $problemCode = [uint32]$propertyMap['DEVPKEY_Device_ProblemCode'] } catch { $problemCode = $null }
  }
  $driverInf = $null
  if ($propertyMap.ContainsKey('DEVPKEY_Device_DriverInfPath')) {
    $driverInf = [System.IO.Path]::GetFileName([string]$propertyMap['DEVPKEY_Device_DriverInfPath'])
  }

  [ordered]@{
    instance_token = Get-Sha256Token -Value $instanceId.ToUpperInvariant()
    status = [string]$Device.Status
    class = [string]$Device.Class
    problem_code = $problemCode
    problem_status = ConvertTo-ProblemStatusHex $propertyMap['DEVPKEY_Device_ProblemStatus']
    service = [string]$propertyMap['DEVPKEY_Device_Service']
    driver_inf = $driverInf
    driver_version = [string]$propertyMap['DEVPKEY_Device_DriverVersion']
  }
}

function Protect-HibikiDiagnosticText {
  param(
    [Parameter(Mandatory)][string]$Text,
    [string]$ComputerName = $env:COMPUTERNAME
  )

  $safe = $Text -replace '(?i)ROOT\\HIBIKIDSP\\[^\s\]\)>,;"'']+', 'ROOT\HIBIKIDSP\<redacted>'
  $safe = $safe -replace '(?i)S-\d-\d+(?:-\d+){1,}', '<sid>'
  $safe = $safe -replace '(?im)\b[A-Z]:\\[^\r\n]*', '<path-redacted>'
  $safe = $safe -replace '(?im)\\\\[^\r\n]*', '<unc-path-redacted>'
  if (-not [string]::IsNullOrWhiteSpace($ComputerName)) {
    $safe = $safe -replace [regex]::Escape($ComputerName), '<computer>'
  }
  $safe
}

function Get-BoundedSetupApiExcerpt {
  param(
    [string[]]$Lines,
    [Parameter(Mandatory)][string]$Needle,
    [Parameter(Mandatory)][int]$MaxLines
  )

  $matchIndices = @()
  for ($index = 0; $index -lt @($Lines).Count; $index++) {
    if ([string]$Lines[$index] -match [regex]::Escape($Needle)) { $matchIndices += $index }
  }
  if ($matchIndices.Count -eq 0) { return @() }

  $lastMatch = $matchIndices[-1]
  $start = $lastMatch
  while ($start -gt 0 -and [string]$Lines[$start] -notmatch '^>>>') { $start-- }
  $end = $lastMatch
  while ($end + 1 -lt $Lines.Count -and [string]$Lines[$end] -notmatch '^<<<') { $end++ }
  $section = @($Lines[$start..$end])
  if ($section.Count -gt $MaxLines) {
    $section = @($section[($section.Count - $MaxLines)..($section.Count - 1)])
  }
  @($section | ForEach-Object { Protect-HibikiDiagnosticText -Text ([string]$_) })
}

function Invoke-DriverPnpInspectionSelfTest {
  $cases = 0
  $directory = [System.IO.FileAttributes]::Directory
  $reparseDirectory = $directory -bor [System.IO.FileAttributes]::ReparsePoint
  $syntheticRoot = [System.IO.Path]::GetFullPath('C:\hibiki-pnp-inspection-selftest\.local').TrimEnd('\', '/')
  $syntheticOutput = Join-Path $syntheticRoot 'reports\pnp.json'
  $syntheticParent = Split-Path -Parent $syntheticOutput
  Assert-InspectionOutputPath -Path $syntheticOutput -Root $syntheticRoot -SyntheticAttributes @{
    $syntheticRoot = $directory
    $syntheticParent = $directory
  }
  $cases++

  $outsideCaught = $false
  try {
    Assert-InspectionOutputPath -Path 'C:\hibiki-pnp-inspection-selftest\pnp.json' -Root $syntheticRoot -SyntheticAttributes @{}
  } catch { $outsideCaught = $_.Exception.Message -match 'must remain under repository \.local' }
  if (-not $outsideCaught) { throw 'Driver PnP inspection self-test expected outside-root rejection.' }
  $cases++

  $reparseCaught = $false
  try {
    Assert-InspectionOutputPath -Path $syntheticOutput -Root $syntheticRoot -SyntheticAttributes @{
      $syntheticRoot = $directory
      $syntheticParent = $reparseDirectory
    }
  } catch { $reparseCaught = $_.Exception.Message -match 'reparse point' }
  if (-not $reparseCaught) { throw 'Driver PnP inspection self-test expected reparse-parent rejection.' }
  $cases++

  $targetDevice = [pscustomobject]@{ InstanceId = 'ROOT\MEDIA\0000'; Status = 'ERROR'; Class = 'MEDIA' }
  $otherDevice = [pscustomobject]@{ InstanceId = 'PCI\VEN_1234\PRIVATE'; Status = 'OK'; Class = 'MEDIA' }
  $properties = @(
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_ProblemCode'; Data = [uint32]10 },
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_ProblemStatus'; Data = [int32]-1073741811 },
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_HardwareIds'; Data = @('Root\HibikiDSP') },
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_Service'; Data = 'HibikiVirtualAudio' },
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_DriverInfPath'; Data = 'oem2.inf' },
    [pscustomobject]@{ KeyName = 'DEVPKEY_Device_DriverVersion'; Data = '1.0.0.0' }
  )
  $summary = ConvertTo-HibikiDeviceSummary -Device $targetDevice -Properties $properties
  if ($null -eq $summary -or $summary.problem_code -ne 10U -or
      $summary.problem_status -ne '0xC000000D' -or $summary.service -ne $targetService -or
      $summary.instance_token.Length -ne 16) {
    throw 'Driver PnP inspection self-test failed typed target-device projection.'
  }
  if ($null -ne (ConvertTo-HibikiDeviceSummary -Device $otherDevice -Properties @())) {
    throw 'Driver PnP inspection self-test accepted an unrelated device.'
  }
  $cases++

  $privateText = 'ROOT\HIBIKIDSP\0000 C:\Users\Alice\driver.inf S-1-5-21-100-200-300-400 MY-GUEST'
  $redacted = Protect-HibikiDiagnosticText -Text $privateText -ComputerName 'MY-GUEST'
  if ($redacted -match '0000|Alice|S-1-5|MY-GUEST') {
    throw 'Driver PnP inspection self-test failed diagnostic redaction.'
  }
  $cases++

  $lines = @('>>> [Device Install - ROOT\HIBIKIDSP\0000]')
  $lines += 1..40 | ForEach-Object { "line $_" }
  $lines += 'problem status 0xC000000D'
  $lines += '<<< Section end'
  $excerpt = @(Get-BoundedSetupApiExcerpt -Lines $lines -Needle $targetHardwareId -MaxLines 16)
  if ($excerpt.Count -gt 16 -or $excerpt[-1] -notmatch 'Section end' -or
      ($excerpt -join "`n") -notmatch '0xC000000D') {
    throw 'Driver PnP inspection self-test failed bounded SetupAPI excerpt behavior.'
  }
  $cases++

  $ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $PSCommandPath, [ref]$null, [ref]$null)
  $commands = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.CommandAst] }, $true) |
    ForEach-Object { $_.GetCommandName() } | Where-Object { $null -ne $_ })
  $forbiddenCommands = @('pnputil.exe', 'Enable-PnpDevice', 'Disable-PnpDevice', 'Restart-PnpDevice',
    'Remove-PnpDevice', 'bcdedit.exe', 'shutdown.exe', 'Restart-Computer')
  if (@($commands | Where-Object { $forbiddenCommands -contains $_ }).Count -ne 0) {
    throw 'Driver PnP inspection self-test found a mutating command in the inspection-only tool.'
  }
  $cases++

  Write-Output "Driver PnP inspection self-test passed ($cases cases: safe output, exact target, typed status, redaction, bounded excerpt, no mutating commands; offline/no PnP access/no file write)."
}

if ($SelfTest) {
  Invoke-DriverPnpInspectionSelfTest
  exit 0
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $localRoot 'driver-pnp-inspection.json'
}
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
Assert-InspectionOutputPath -Path $resolvedOutput -Root $localRoot

$pnpDeviceCommand = Get-Command Get-PnpDevice -ErrorAction SilentlyContinue
$pnpPropertyCommand = Get-Command Get-PnpDeviceProperty -ErrorAction SilentlyContinue
$devices = @()
$pnpError = $null
if ($null -ne $pnpDeviceCommand -and $null -ne $pnpPropertyCommand) {
  try {
    $allDevices = @(Get-PnpDevice -ErrorAction Stop)
    $candidateDevices = @($allDevices | Where-Object {
      [string]$_.InstanceId -match $targetInstancePattern -or [string]$_.Class -eq 'MEDIA'
    })
    foreach ($device in $candidateDevices) {
      $properties = @()
      $propertyError = $null
      try {
        $properties = @(Get-PnpDeviceProperty -InstanceId ([string]$device.InstanceId) -KeyName @(
            'DEVPKEY_Device_ProblemCode',
            'DEVPKEY_Device_ProblemStatus',
            'DEVPKEY_Device_HardwareIds',
            'DEVPKEY_Device_Service',
            'DEVPKEY_Device_DriverInfPath',
            'DEVPKEY_Device_DriverVersion'
          ) -ErrorAction Stop)
      }
      catch {
        $propertyError = 'property-query-failed'
      }
      $summary = ConvertTo-HibikiDeviceSummary -Device $device -Properties $properties
      if ($null -ne $summary) {
        $summary['property_error'] = $propertyError
        $devices += $summary
      }
    }
  }
  catch {
    $pnpError = 'pnp-query-failed'
    $devices = @()
  }
}

$serviceSummary = $null
$serviceError = $null
try {
  $service = Get-Service -Name $targetService -ErrorAction Stop
  $serviceSummary = [ordered]@{
    name = $targetService
    state = [string]$service.Status
    start_type = [string]$service.StartType
  }
}
catch {
  $serviceError = 'service-unavailable'
}

$setupApiPath = Join-Path $env:SystemRoot 'INF\setupapi.dev.log'
$setupApiExcerpt = @()
$setupApiError = $null
if (Test-Path -LiteralPath $setupApiPath -PathType Leaf) {
  try {
    $setupApiTail = @(Get-Content -LiteralPath $setupApiPath -Tail 4000 -ErrorAction Stop)
    $setupApiExcerpt = @(Get-BoundedSetupApiExcerpt -Lines $setupApiTail -Needle $targetHardwareId `
      -MaxLines $SetupApiMaxLines)
  }
  catch {
    $setupApiError = 'setupapi-read-failed'
  }
}
else {
  $setupApiError = 'setupapi-unavailable'
}

$report = [ordered]@{
  schema_version = 1
  generated_at_utc = [DateTime]::UtcNow.ToString('o')
  mode = 'inspection-only'
  mutation_attempted = $false
  target = [ordered]@{
    hardware_id = $targetHardwareId
    service = $targetService
  }
  environment = [ordered]@{
    os_build = [System.Environment]::OSVersion.Version.Build
    architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
  }
  availability = [ordered]@{
    pnp_cmdlets = ($null -ne $pnpDeviceCommand -and $null -ne $pnpPropertyCommand)
    pnp_error = $pnpError
    service_error = $serviceError
    setupapi_error = $setupApiError
  }
  device_count = @($devices).Count
  devices = @($devices)
  service = $serviceSummary
  setupapi = [ordered]@{
    source = 'setupapi.dev.log tail (sanitized)'
    max_lines = $SetupApiMaxLines
    excerpt = @($setupApiExcerpt)
  }
  claims = @(
    'Read-only guest/host inspection; no device or boot state was changed.',
    'Absence of a matching device is reported as device_count=0 and is not a successful driver-load claim.'
  )
}

$outputParent = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
$json = $report | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($resolvedOutput, $json + [Environment]::NewLine,
  [System.Text.UTF8Encoding]::new($false))
Write-Output "Driver PnP inspection completed (read-only; devices=$(@($devices).Count); output=$resolvedOutput)."
