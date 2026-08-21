@echo off
setlocal
cd /d "%~dp0"
where pwsh >nul 2>&1
if errorlevel 1 (
  echo PowerShell 7 is required. Run: pwsh -File tools\run-preview.ps1 -Build
  pause
  exit /b 1
)
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run-preview.ps1" -Build
if errorlevel 1 pause
