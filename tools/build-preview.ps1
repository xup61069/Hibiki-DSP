[CmdletBinding()]
param(
  [ValidateSet('WinUI', 'WinUICompat', 'DesktopCompat', 'ControlModel')][string]$Target = 'WinUI'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$previewRoot = Join-Path $repo ".local/preview/$Target"

if ($Target -eq 'ControlModel') {
  $project = Join-Path $repo 'apps/control-model-check/Hibiki.ControlModel.Check.csproj'
  dotnet run --project $project --configuration Release --nologo `
    "-p:BaseOutputPath=$previewRoot/bin/" `
    "-p:MSBuildProjectExtensionsPath=$previewRoot/obj/"
  if ($LASTEXITCODE -ne 0) { throw "Control-model preview baseline failed: $LASTEXITCODE" }
  Write-Output "Control-model preview baseline passed. Output stays under $previewRoot and is not publishable."
  return
}

if ($Target -eq 'WinUICompat') {
  # This fallback is intentionally explicit: it is useful to preview the
  # ViewModel on a machine where the App SDK XAML compiler cannot run, but it
  # is not a substitute for the target WinUI/XAML accessibility gate.
  $project = Join-Path $repo 'apps/winui-shell/Hibiki.WinUI.csproj'
  dotnet build $project --configuration Release "-p:OutputPath=$previewRoot/" '-p:HibikiCompatibilityPreview=true' '-p:Platform=x64'
  if ($LASTEXITCODE -ne 0) { throw "Compatibility preview build failed: $LASTEXITCODE" }
  Write-Output "Compatibility WinUI preview build succeeded. It is unsigned, driver-free, not release evidence and excluded from Git."
  return
}

if ($Target -eq 'DesktopCompat') {
  # A self-contained .NET fallback for machines without Windows App Runtime.
  # It shares the control model, but is not the formal WinUI shell.
  $project = Join-Path $repo 'apps/desktop-compat-preview/Hibiki.DesktopPreview.csproj'
  dotnet publish $project --configuration Release --runtime win-x64 --self-contained true --output $previewRoot
  if ($LASTEXITCODE -ne 0) { throw "Desktop compatibility preview build failed: $LASTEXITCODE" }
  Write-Output "Self-contained desktop preview build succeeded. It needs no Windows App Runtime, is driver-free and excluded from Git."
  return
}

# This is deliberately a local developer preview only. It does not build a
# virtual driver, sign anything, stage an installer, or create a GitHub asset.
$project = Join-Path $repo 'apps/winui-shell/Hibiki.WinUI.csproj'
dotnet build $project --configuration Release "-p:OutputPath=$previewRoot/"
if ($LASTEXITCODE -ne 0) {
  throw "WinUI preview build failed. Check tools/doctor.ps1; target Windows 11 24H2, VS 2026 and the locked SDK/WDK are required before treating a build as preview evidence."
}
Write-Output "WinUI local preview build succeeded. It remains unsigned, driver-free and excluded from Git."
