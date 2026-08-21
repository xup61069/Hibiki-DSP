[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repo '.local/engine-preview'

cmake -S $repo -B $buildRoot '-DHIBIKI_BUILD_ENGINE_PREVIEW=ON' '-DHIBIKI_BUILD_TESTS=OFF'
if ($LASTEXITCODE -ne 0) { throw "Engine preview configure failed: $LASTEXITCODE" }
cmake --build $buildRoot --config Release --target hibiki_engine_preview
if ($LASTEXITCODE -ne 0) { throw "Engine preview build failed: $LASTEXITCODE" }
Write-Output "Engine Preview build succeeded. Start the executable from $buildRoot before connecting DesktopCompat."
