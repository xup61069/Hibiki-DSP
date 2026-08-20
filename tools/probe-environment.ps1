[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$local = Join-Path $repo '.local'
New-Item -ItemType Directory -Path $local -Force | Out-Null
$os = Get-CimInstance Win32_OperatingSystem
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$git = Get-Command git -ErrorAction SilentlyContinue

$probe = [ordered]@{
  schema_version = 1
  captured_at_utc = [DateTime]::UtcNow.ToString('o')
  os = [ordered]@{ caption = $os.Caption; build = [int]$os.BuildNumber; architecture = $env:PROCESSOR_ARCHITECTURE }
  tools = [ordered]@{
    cmake = if ($cmake) { & cmake --version | Select-Object -First 1 } else { $null }
    git = if ($git) { & git --version } else { $null }
  }
  audio = [ordered]@{
    note = 'Capture actual endpoint IDs and private calibration only in local user storage; never commit them.'
    supported_formats = @('LPCM 2.0', 'LPCM 5.1', 'LPCM 7.1')
  }
}

$probe | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $local 'context.json') -Encoding utf8
Write-Output "Wrote local environment fingerprint: $local/context.json"
