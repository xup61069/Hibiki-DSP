#Requires -Version 7
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$EnginePath,
  [Parameter(Mandatory)][string]$WorkingDirectory,
  [Parameter(Mandatory)][string]$SourcePath,
  [Parameter(Mandatory)][string]$RenderPath,
  [Parameter(Mandatory)][string]$StdoutCapturePath,
  [Parameter(Mandatory)][string]$StderrCapturePath,
  [int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$argumentList = @(
  '--render-offline', $RenderPath,
  '--enable-wav-source',
  '--wav-source-path', $SourcePath
)
# capture paths are supplied by the caller
$process = Start-Process -FilePath $EnginePath -ArgumentList $argumentList `
  -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput $StdoutCapturePath -RedirectStandardError $StderrCapturePath
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
  try { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue } catch { }
  throw "offline render worker timed out after $TimeoutSeconds seconds."
}
$stdoutText = Get-Content -LiteralPath $StdoutCapturePath -ErrorAction SilentlyContinue
if ($stdoutText) { $stdoutText | Write-Output }
$stderrText = Get-Content -LiteralPath $StderrCapturePath -ErrorAction SilentlyContinue
if ($stderrText) { $stderrText | Write-Warning }
exit $process.ExitCode
