#Requires -Version 7

<#
.SYNOPSIS
    Hosted-CI byte verification for the committed WinUI theme-fallback block.

.DESCRIPTION
    Obtains the pinned microsoft.windowsappsdk package from the nuget.org flat
    container (or verifies a caller-provided nupkg via -PackagePath), fail-closed
    hash-verifies the package (SHA-512) and the extracted framework generic.xaml
    (SHA-256), then invokes tools/extract-theme-fallbacks.ps1 -VerifyCommitted
    against the extracted file. This closes the wave 59 boundary: the
    byte-for-byte verification of the committed App.xaml.cs fallback rows no
    longer depends on an authoring-machine NuGet cache.

    The gate writes only inside a freshly created temporary directory and
    removes it on exit; the repository is never modified.

.PARAMETER PackagePath
    Optional path to an existing microsoft.windowsappsdk nupkg. When omitted the
    pinned package is downloaded from the nuget.org flat container with bounded
    retries.

.PARAMETER SelfTest
    Validates the gate logic without network access and without writing files:
    pinned-hash constant shape, csproj version coupling, flat-container URL
    construction, in-memory zip entry selection and streaming hash, and the
    wiring to the extractor parameters.

.EXAMPLE
    pwsh -NoProfile -File tools/verify-theme-fallbacks.ps1

.EXAMPLE
    pwsh -NoProfile -File tools/verify-theme-fallbacks.ps1 -PackagePath <nupkg>

.EXAMPLE
    pwsh -NoProfile -File tools/verify-theme-fallbacks.ps1 -SelfTest
#>
param(
    [string]$PackagePath,
    [switch]$SelfTest
)

$script:PinnedVersion = '1.7.250401001'
$script:PinnedNupkgSha512Hex = '531565343791a64027a143690d0c51e3492eda982d13caa6be2015c53109860bf06c8d5b69576836d2ff37d69796cde3668a6af320608d1f8bbd425680655593'
$script:PinnedGenericXamlSha256Hex = '0a32014884636efe31fe7d71f6c620705e3d5fc3a90add1ee93e03f478912408'
$script:PinnedXamlEntryPath = 'lib/net6.0-windows10.0.22621.0/Microsoft.WinUI/Themes/generic.xaml'
$script:NuGetFlatContainerUrl = 'https://api.nuget.org/v3-flatcontainer/microsoft.windowsappsdk/{0}/microsoft.windowsappsdk.{0}.nupkg'

function Get-RepoRootPath {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-ExtractorScriptPath {
    param([string]$Root)
    return (Join-Path $Root 'tools/extract-theme-fallbacks.ps1')
}

function Resolve-PinnedPackageVersion {
    param([string]$Root)
    $csproj = Join-Path $Root 'apps/winui-shell/Hibiki.WinUI.csproj'
    if (-not (Test-Path -LiteralPath $csproj -PathType Leaf)) { throw 'project file not found: ' + $csproj }
    $csprojText = [System.IO.File]::ReadAllText($csproj)
    if ($csprojText -notmatch 'WindowsAppSDKVersion[^>]*>([^<]+)<') { throw 'WindowsAppSDKVersion not found in ' + $csproj }
    return $Matches[1].Trim()
}

function Get-NuGetPackageUrl {
    param([string]$Version)
    return ($script:NuGetFlatContainerUrl -f $Version)
}

function Get-ExpectedNupkgFileName {
    param([string]$Version)
    return 'microsoft.windowsappsdk.' + $Version + '.nupkg'
}

function Assert-HexDigestShape {
    param([string]$Digest, [int]$Length, [string]$Label)
    if ($Digest.Length -ne $Length) { throw $Label + ' must be ' + $Length + ' hex characters, got ' + $Digest.Length }
    if ($Digest -notmatch '^[0-9a-f]+$') { throw $Label + ' must be lowercase hexadecimal' }
}

function Get-FileShaHex {
    param([string]$Path, [string]$Algorithm)
    $hash = Get-FileHash -LiteralPath $Path -Algorithm $Algorithm
    return $hash.Hash.ToLowerInvariant()
}

function Get-StreamSha256Hex {
    param([System.IO.Stream]$Stream)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash($Stream)
        return ([System.BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-PinnedXamlArchiveEntry {
    param([System.IO.Compression.ZipArchive]$Archive)
    $entryMatches = @($Archive.Entries | Where-Object { $_.FullName -eq $script:PinnedXamlEntryPath })
    if ($entryMatches.Count -eq 0) { throw 'pinned entry not found in package: ' + $script:PinnedXamlEntryPath }
    if ($entryMatches.Count -gt 1) { throw 'pinned entry is ambiguous in package: ' + $script:PinnedXamlEntryPath }
    return $entryMatches[0]
}

function Save-PinnedXamlEntry {
    param([System.IO.Compression.ZipArchiveEntry]$Entry, [string]$DestinationPath)
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $DestinationPath, $true)
}

function Invoke-DownloadWithRetry {
    param([string]$Url, [string]$DestinationPath)
    $attempt = 0
    $maxAttempts = 3
    while ($true) {
        $attempt++
        try {
            Invoke-WebRequest -Uri $Url -OutFile $DestinationPath -MaximumRetryCount 2 -RetryIntervalSec 3 -TimeoutSec 300
            return
        } catch {
            if ($attempt -ge $maxAttempts) {
                throw ('failed to download pinned package after ' + $maxAttempts + ' attempts: ' + $Url)
            }
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
}

function Assert-ExtractorWiring {
    param([string]$Root)
    $extractor = Get-ExtractorScriptPath -Root $Root
    if (-not (Test-Path -LiteralPath $extractor -PathType Leaf)) { throw 'extractor script not found: ' + $extractor }
    $command = Get-Command -Name $extractor -ErrorAction SilentlyContinue
    if ($null -eq $command) { throw 'extractor script could not be loaded as a command: ' + $extractor }
    foreach ($name in @('XamlPath', 'VerifyCommitted')) {
        if (-not $command.Parameters.ContainsKey($name)) { throw 'extractor is missing the ' + $name + ' parameter required by this gate' }
    }
}

function Invoke-CommittedVerification {
    param([string]$Root, [string]$XamlPath)
    $extractor = Get-ExtractorScriptPath -Root $Root
    & pwsh -NoProfile -File $extractor -VerifyCommitted -XamlPath $XamlPath
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw 'extract-theme-fallbacks -VerifyCommitted failed with exit code ' + $exitCode }
}

if ($SelfTest) {
    $caseCount = 0

    # Case 1: pinned constants are well-formed and internally consistent.
    Assert-HexDigestShape -Digest $script:PinnedNupkgSha512Hex -Length 128 -Label 'pinned nupkg SHA-512'
    Assert-HexDigestShape -Digest $script:PinnedGenericXamlSha256Hex -Length 64 -Label 'pinned generic.xaml SHA-256'
    if ($script:PinnedVersion -notmatch '^\d+\.\d+\.\d+([^.]+)?$') { throw 'pinned version is not a valid NuGet version: ' + $script:PinnedVersion }
    if ($script:PinnedXamlEntryPath -notmatch '^lib/.+/Microsoft\.WinUI/Themes/generic\.xaml$') { throw 'pinned xaml entry path is not under the expected framework themes directory' }
    $caseCount++

    # Case 2: the csproj version pin must stay coupled to the gate constants.
    $repoRoot = Get-RepoRootPath
    $csprojVersion = Resolve-PinnedPackageVersion -Root $repoRoot
    if ($csprojVersion -ne $script:PinnedVersion) { throw 'WindowsAppSDKVersion ' + $csprojVersion + ' drifted from the gate pin ' + $script:PinnedVersion + '; update the pinned hashes together with the version' }
    $caseCount++

    # Case 3: flat-container URL and package file name construction.
    $expectedUrl = 'https://api.nuget.org/v3-flatcontainer/microsoft.windowsappsdk/' + $script:PinnedVersion + '/microsoft.windowsappsdk.' + $script:PinnedVersion + '.nupkg'
    if ((Get-NuGetPackageUrl -Version $script:PinnedVersion) -ne $expectedUrl) { throw 'flat-container URL construction drifted' }
    if ((Get-ExpectedNupkgFileName -Version $script:PinnedVersion) -ne 'microsoft.windowsappsdk.' + $script:PinnedVersion + '.nupkg') { throw 'package file name construction drifted' }
    $caseCount++

    # Case 4: in-memory zip entry selection and streaming hash (no files written).
    Add-Type -AssemblyName System.IO.Compression
    $fixtureStream = New-Object System.IO.MemoryStream
    try {
        $fixture = New-Object System.IO.Compression.ZipArchive($fixtureStream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            $fixtureBytesMain = [System.Text.Encoding]::UTF8.GetBytes('fixture-generic-xaml-content')
            $entryMain = $fixture.CreateEntry($script:PinnedXamlEntryPath)
            $streamMain = $entryMain.Open()
            $streamMain.Write($fixtureBytesMain, 0, $fixtureBytesMain.Length)
            $streamMain.Dispose()
            $fixtureBytesDecoy = [System.Text.Encoding]::UTF8.GetBytes('decoy-content')
            $entryDecoy = $fixture.CreateEntry('lib/other/Microsoft.WinUI/Themes/generic.xaml')
            $streamDecoy = $entryDecoy.Open()
            $streamDecoy.Write($fixtureBytesDecoy, 0, $fixtureBytesDecoy.Length)
            $streamDecoy.Dispose()
        } finally { $fixture.Dispose() }
        $fixtureStream.Position = 0
        $readArchive = New-Object System.IO.Compression.ZipArchive($fixtureStream, [System.IO.Compression.ZipArchiveMode]::Read, $true)
        try {
            $selected = Get-PinnedXamlArchiveEntry -Archive $readArchive
            if ($selected.FullName -ne $script:PinnedXamlEntryPath) { throw 'zip entry selector picked the wrong entry' }
            $expectedSha = ([System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::HashData($fixtureBytesMain))).Replace('-', '').ToLowerInvariant()
            $streamSelected = $selected.Open()
            try {
                $actualSha = Get-StreamSha256Hex -Stream $streamSelected
            } finally { $streamSelected.Dispose() }
            if ($actualSha -ne $expectedSha) { throw 'entry stream hash helper drifted from the direct computation' }
        } finally { $readArchive.Dispose() }
    } finally { $fixtureStream.Dispose() }
    $caseCount++

    # Case 5: a package without the pinned entry fails closed.
    $missingStream = New-Object System.IO.MemoryStream
    try {
        $missingArchive = New-Object System.IO.Compression.ZipArchive($missingStream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
        $unusedEntry = $missingArchive.CreateEntry('unrelated/file.txt')
        $missingArchive.Dispose()
        $missingStream.Position = 0
        $readMissing = New-Object System.IO.Compression.ZipArchive($missingStream, [System.IO.Compression.ZipArchiveMode]::Read, $true)
        try {
            $rejected = $false
            try { Get-PinnedXamlArchiveEntry -Archive $readMissing | Out-Null } catch { $rejected = $true }
            if (-not $rejected) { throw 'missing pinned entry did not fail closed' }
        } finally { $readMissing.Dispose() }
    } finally { $missingStream.Dispose() }
    $caseCount++

    # Case 6: extractor wiring (parameters this gate depends on).
    Assert-ExtractorWiring -Root $repoRoot
    $caseCount++

    Write-Output ('verify-theme-fallbacks self-test passed (' + $caseCount + ' case groups: constants, version coupling, url construction, zip entry selection, missing-entry fail-closed, extractor wiring)')
    exit 0
}

$repoRoot = Get-RepoRootPath
$csprojVersion = Resolve-PinnedPackageVersion -Root $repoRoot
if ($csprojVersion -ne $script:PinnedVersion) { throw 'WindowsAppSDKVersion ' + $csprojVersion + ' drifted from the gate pin ' + $script:PinnedVersion + '; update the pinned hashes together with the version' }

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ('hibiki-verify-theme-fallbacks-' + [guid]::NewGuid().ToString('N'))
$createdTemp = $false
try {
    if ($PackagePath) {
        if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw 'package path not found: ' + $PackagePath }
        $nupkgPath = (Resolve-Path -LiteralPath $PackagePath).Path
        $packageSource = 'local: ' + $nupkgPath
    } else {
        [System.IO.Directory]::CreateDirectory($tempDir) | Out-Null
        $createdTemp = $true
        $nupkgPath = Join-Path $tempDir (Get-ExpectedNupkgFileName -Version $script:PinnedVersion)
        $packageUrl = Get-NuGetPackageUrl -Version $script:PinnedVersion
        Write-Output ('source: ' + $packageUrl)
        Invoke-DownloadWithRetry -Url $packageUrl -DestinationPath $nupkgPath
        $packageSource = $packageUrl
    }

    $nupkgSha = Get-FileShaHex -Path $nupkgPath -Algorithm 'SHA512'
    if ($nupkgSha -ne $script:PinnedNupkgSha512Hex) { throw 'nupkg SHA-512 mismatch: expected ' + $script:PinnedNupkgSha512Hex + ', got ' + $nupkgSha }
    Write-Output 'nupkg sha512: ok'

    Add-Type -AssemblyName System.IO.Compression
    $archive = [System.IO.Compression.ZipFile]::OpenRead($nupkgPath)
    try {
        $xamlEntry = Get-PinnedXamlArchiveEntry -Archive $archive
        if (-not $createdTemp) {
            [System.IO.Directory]::CreateDirectory($tempDir) | Out-Null
            $createdTemp = $true
        }
        $xamlPath = Join-Path $tempDir 'generic.xaml'
        Save-PinnedXamlEntry -Entry $xamlEntry -DestinationPath $xamlPath
    } finally { $archive.Dispose() }

    $xamlSha = Get-FileShaHex -Path $xamlPath -Algorithm 'SHA256'
    if ($xamlSha -ne $script:PinnedGenericXamlSha256Hex) { throw 'generic.xaml SHA-256 mismatch: expected ' + $script:PinnedGenericXamlSha256Hex + ', got ' + $xamlSha }
    Write-Output 'generic.xaml sha256: ok'

    Invoke-CommittedVerification -Root $repoRoot -XamlPath $xamlPath
    Write-Output 'theme fallback CI verification passed (pinned package hash-verified, committed block byte-for-byte)'
} finally {
    if ($createdTemp -and (Test-Path -LiteralPath $tempDir)) {
        Remove-Item -LiteralPath $tempDir -Recurse -Force
    }
}
