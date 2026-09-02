#Requires -Version 7

<#
.SYNOPSIS
    Regenerates the committed WinUI compat theme-fallback dictionary rows.

.DESCRIPTION
    Extracts typed resource entries from the pinned WindowsAppSDK
    generic.xaml and emits the exact C# rows committed in
    apps/winui-shell/App.xaml.cs between the generated-block markers.
    The extraction reproduces the original #2250 machine-local extractor,
    including its documented quirks:

    - The Light theme dictionary wins over root-level entries and the first
      definition of a key wins inside each scope.
    - StaticResource alias chains that terminate in a LinearGradientBrush
      degrade to the elevation approximation color; chains that terminate in
      an AcrylicBrush or DesktopAcrylicBackdrop degrade to the acrylic
      approximation color.
    - {ThemeResource ...} color references are preserved verbatim.
    - Six-digit hex colors gain an #FF alpha prefix (case preserved) and
      three-digit hex colors expand each digit.
    - Thickness and CornerRadius values reproduce the original tool quirk:
      a single value repeats its FIRST CHARACTER four times, two or three
      values produce four empty components, and four values pass through
      verbatim.

    Rows are emitted as two invariant-culture (case-insensitive) sorted
    runs: base rows first, then the approximation rows. This ordering was
    verified byte-for-byte against the committed block; a plain ordinal
    sort does NOT reproduce it.

.PARAMETER XamlPath
    Overrides the source generic.xaml path. By default the pinned
    WindowsAppSDK version is resolved from
    apps/winui-shell/Hibiki.WinUI.csproj and the file is located in the
    NuGet package cache.

.PARAMETER AppPath
    Overrides the committed App.xaml.cs path used by -VerifyCommitted and
    -SelfTest. Defaults to apps/winui-shell/App.xaml.cs in this repository.

.PARAMETER VerifyCommitted
    Re-extracts the rows from the pinned framework XAML and byte-compares
    them against the committed block. Requires the pinned NuGet package on
    disk. Writes no files.

.PARAMETER SelfTest
    Runs embedded synthetic fixtures covering every extraction rule plus
    structural invariants of the committed block (markers, row count, row
    format, unique keys, token prefixes, and the two-run ordering). Uses
    only tracked sources and writes no files.

.EXAMPLE
    pwsh -NoProfile -File tools/extract-theme-fallbacks.ps1 -SelfTest

.EXAMPLE
    pwsh -NoProfile -File tools/extract-theme-fallbacks.ps1 -VerifyCommitted

.EXAMPLE
    pwsh -NoProfile -File tools/extract-theme-fallbacks.ps1
#>

param(
    [string]$XamlPath,
    [string]$AppPath,
    [switch]$VerifyCommitted,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetDirectoryName($PSScriptRoot)
if (-not $AppPath) {
    $AppPath = Join-Path $repoRoot 'apps/winui-shell/App.xaml.cs'
}

$script:Xns = 'http://schemas.microsoft.com/winfx/2006/xaml'
$script:BeginMarker = '// >>> BEGIN GENERATED COMPAT THEME FALLBACKS >>>'
$script:EndMarker = '// <<< END GENERATED COMPAT THEME FALLBACKS <<<'
$script:ExpectedRowCount = 3242
$script:ElevationColor = 'C:#14000000'
$script:AcrylicColor = 'C:#F2FFFFFF'
$script:AllowedPrefixes = @('C', 'T', 'D', 'Y', 'S', 'R', 'W', 'B', 'F', 'E', 'G', 'A', 'H', 'V')
$script:SkipTags = @('AcrylicBrush', 'LinearGradientBrush', 'DataTemplate', 'ThemeShadow', 'RatingItemFontInfo', 'DesktopAcrylicBackdrop', 'TextCommandBarFlyout', 'Int32')

$script:Light = @{}
$script:Top = @{}
$script:Resolving = [System.Collections.Generic.HashSet[string]]::new()

function Get-SortedKeys {
    param([System.Collections.IEnumerable]$Keys)
    $list = [System.Collections.Generic.List[string]]::new()
    foreach ($k in $Keys) { [void]$list.Add([string]$k) }
    $arr = $list.ToArray()
    [Array]::Sort($arr, [System.StringComparer]::InvariantCultureIgnoreCase)
    return $arr
}

function Import-ResourceDictionary {
    param([System.Xml.XmlDocument]$Document)
    $script:Light = @{}
    $script:Top = @{}
    $script:Resolving = [System.Collections.Generic.HashSet[string]]::new()
    $root = $Document.DocumentElement
    foreach ($node in $root.ChildNodes) {
        if ($node.NodeType -ne 'Element') { continue }
        if ($node.LocalName -eq 'ResourceDictionary.ThemeDictionaries') {
            foreach ($d in $node.ChildNodes) {
                if ($d.NodeType -ne 'Element') { continue }
                if ($d.GetAttribute('Key', $script:Xns) -ne 'Light') { continue }
                foreach ($c in $d.ChildNodes) {
                    if ($c.NodeType -ne 'Element') { continue }
                    $ck = $c.GetAttribute('Key', $script:Xns)
                    if ($ck -and -not $script:Light.ContainsKey($ck)) { $script:Light[$ck] = $c }
                }
            }
            continue
        }
        $k = $node.GetAttribute('Key', $script:Xns)
        if ($k -and -not $script:Top.ContainsKey($k)) { $script:Top[$k] = $node }
    }
}

function Get-Resource([string]$key) {
    if ($script:Light.ContainsKey($key)) { return $script:Light[$key] }
    if ($script:Top.ContainsKey($key)) { return $script:Top[$key] }
    return $null
}

function Get-RefKey([string]$value) {
    if ($value -match '^\{(ThemeResource|StaticResource)\s+([^}]+)\}$') { return $Matches[2].Trim() }
    return $null
}

function Normalize-Hex([string]$value) {
    $t = $value.Trim()
    if ($t -match '^#[0-9A-Fa-f]{6}$') { return '#FF' + $t.Substring(1) }
    if ($t -match '^#[0-9A-Fa-f]{3}$') {
        $r = $t.Substring(1, 1)
        $g = $t.Substring(2, 1)
        $b = $t.Substring(3, 1)
        return '#FF' + ($r + $r) + ($g + $g) + ($b + $b)
    }
    return $t
}

function Get-ColorFromKey([string]$key, [string]$refText) {
    if (-not $script:Resolving.Add($key)) { return $null }
    try {
        $el = Get-Resource $key
        if (-not $el) { return 'C:' + $refText }
        $tag = $el.LocalName
        if ($tag -eq 'SolidColorBrush' -or $tag -eq 'Color') { return Get-ColorValue $el }
        if ($tag -eq 'StaticResource') { return Get-ColorFromKey ($el.GetAttribute('ResourceKey')) $refText }
        if ($tag -eq 'LinearGradientBrush') { return $script:ElevationColor }
        if ($tag -eq 'AcrylicBrush' -or $tag -eq 'DesktopAcrylicBackdrop') { return $script:AcrylicColor }
        return $null
    } finally {
        [void]$script:Resolving.Remove($key)
    }
}

function Get-ColorValue([System.Xml.XmlElement]$el) {
    $tag = $el.LocalName
    if ($tag -eq 'SolidColorBrush') {
        $c = $el.GetAttribute('Color')
        $refKey = Get-RefKey $c
        if ($refKey) { return Get-ColorFromKey $refKey $c }
        return 'C:' + (Normalize-Hex $c)
    }
    if ($tag -eq 'Color') {
        $t = $el.InnerText.Trim()
        $refKey = Get-RefKey $t
        if ($refKey) { return Get-ColorFromKey $refKey $t }
        return 'C:' + (Normalize-Hex $t)
    }
    return $null
}

function Expand-Parts([string]$value, [int]$want) {
    # Faithful reproduction of the original #2250 extractor quirks observed
    # in the committed dictionary: a single value collapses to its FIRST
    # CHARACTER repeated four times (the original pipeline unrolled the
    # scalar so the index hit the string itself), two or three values emit
    # four empty components, and four values pass through verbatim.
    $parts = @($value -split ',') | ForEach-Object { $_.Trim() }
    if ($parts.Count -eq 1) {
        $scalar = [string]$parts
        if ($scalar.Length -lt 1) { throw 'empty single-part Thickness/CornerRadius value' }
        $c = $scalar.Substring(0, 1)
        return (@($c) * $want) -join ','
    }
    if ($parts.Count -eq 4) { return ($parts -join ',') }
    return (@('', '', '', '')) -join ','
}

function Get-ApproxFromChain([string]$key) {
    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $cur = $key
    while ($true) {
        if (-not $seen.Add($cur)) { return $null }
        $el = Get-Resource $cur
        if (-not $el) { return $null }
        $tag = $el.LocalName
        if ($tag -eq 'LinearGradientBrush') { return $script:ElevationColor }
        if ($tag -eq 'AcrylicBrush' -or $tag -eq 'DesktopAcrylicBackdrop') { return $script:AcrylicColor }
        if ($tag -eq 'StaticResource') { $cur = $el.GetAttribute('ResourceKey'); continue }
        return $null
    }
}

function Get-Token([string]$key) {
    if (-not $script:Resolving.Add($key)) { return $null }
    try {
        $el = Get-Resource $key
        if (-not $el) { return $null }
        $tag = $el.LocalName
        switch ($tag) {
            'StaticResource' {
                $target = $el.GetAttribute('ResourceKey')
                $approx = Get-ApproxFromChain $target
                if ($approx) { return $approx }
                return Get-Token $target
            }
            'SolidColorBrush' { return Get-ColorValue $el }
            'Color' { return Get-ColorValue $el }
            'Double' { return 'D:' + $el.InnerText.Trim() }
            'Boolean' { return 'B:' + $el.InnerText.Trim() }
            'String' { return 'S:' + $el.InnerText }
            'Thickness' { return 'T:' + (Expand-Parts ($el.InnerText.Trim()) 4) }
            'CornerRadius' { return 'R:' + (Expand-Parts ($el.InnerText.Trim()) 4) }
            'GridLength' { return 'G:' + $el.InnerText.Trim() }
            'FontWeight' { return 'W:' + $el.InnerText.Trim() }
            'HorizontalAlignment' { return 'H:' + $el.InnerText.Trim() }
            'VerticalAlignment' { return 'A:' + $el.InnerText.Trim() }
            'Visibility' { return 'V:' + $el.InnerText.Trim() }
            'FontFamily' { return 'F:' + $el.InnerText.Trim() }
            'Style' {
                $tt = $el.GetAttribute('TargetType')
                if ($tt) { return 'Y:' + $tt }
                return $null
            }
            default {
                if ($script:SkipTags -contains $tag -or $tag -like '*Converter') { return $null }
                $t = $el.InnerText.Trim()
                if ($t) { return 'E:' + $tag + ':' + $t }
                return $null
            }
        }
    } finally {
        [void]$script:Resolving.Remove($key)
    }
}

function Get-FallbackRows {
    $script:Resolving = [System.Collections.Generic.HashSet[string]]::new()
    $allKeys = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($k in $script:Light.Keys) { [void]$allKeys.Add([string]$k) }
    foreach ($k in $script:Top.Keys) { [void]$allKeys.Add([string]$k) }
    $base = [ordered]@{}
    $approx = [ordered]@{}
    foreach ($k in (Get-SortedKeys $allKeys)) {
        $t = Get-Token $k
        if (-not $t) { continue }
        if ($t -ceq $script:ElevationColor -or $t -ceq $script:AcrylicColor) {
            $approx[$k] = $t
        } else {
            $base[$k] = $t
        }
    }
    $rows = [System.Collections.Generic.List[string]]::new()
    foreach ($k in (Get-SortedKeys $base.Keys)) {
        [void]$rows.Add('            ("' + $k + '", "' + $base[$k] + '"),')
    }
    foreach ($k in (Get-SortedKeys $approx.Keys)) {
        [void]$rows.Add('            ("' + $k + '", "' + $approx[$k] + '"),')
    }
    return $rows
}

function Resolve-DefaultXamlPath {
    $csproj = Join-Path $repoRoot 'apps/winui-shell/Hibiki.WinUI.csproj'
    if (-not (Test-Path -LiteralPath $csproj -PathType Leaf)) { throw 'project file not found: ' + $csproj }
    $csprojText = [System.IO.File]::ReadAllText($csproj)
    if ($csprojText -notmatch 'WindowsAppSDKVersion[^>]*>([^<]+)<') {
        throw 'WindowsAppSDKVersion not found in ' + $csproj
    }
    $version = $Matches[1].Trim()
    $roots = [System.Collections.Generic.List[string]]::new()
    if ($env:NUGET_PACKAGES) { [void]$roots.Add($env:NUGET_PACKAGES) }
    [void]$roots.Add([System.IO.Path]::Combine($env:USERPROFILE, '.nuget', 'packages'))
    foreach ($pkgRoot in $roots) {
        $libDir = [System.IO.Path]::Combine($pkgRoot, 'microsoft.windowsappsdk', $version, 'lib')
        if (-not (Test-Path -LiteralPath $libDir -PathType Container)) { continue }
        $hits = @(
            Get-ChildItem -LiteralPath $libDir -Recurse -Filter 'generic.xaml' -File -ErrorAction SilentlyContinue |
                Where-Object { ($_.FullName -replace '\\', '/') -like '*Microsoft.WinUI/Themes/generic.xaml' }
        )
        if ($hits.Count -eq 0) { continue }
        $chosen = $null
        foreach ($h in $hits) {
            if (($h.FullName -replace '\\', '/') -like '*net6.0-windows10.0.22621.0*') { $chosen = $h; break }
        }
        if (-not $chosen) { $chosen = $hits[0] }
        return $chosen.FullName
    }
    throw ('pinned WindowsAppSDK ' + $version + ' generic.xaml was not found under the NuGet package cache; pass -XamlPath explicitly')
}

function Import-XamlFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'source XAML not found: ' + $Path }
    $doc = New-Object System.Xml.XmlDocument
    $doc.Load($Path)
    Import-ResourceDictionary -Document $doc
}

function Get-CommittedRegion {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'committed file not found: ' + $Path }
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $fileText = [System.IO.File]::ReadAllText($Path, $utf8)
    $crnl = [string][char]13 + [char]10
    $nl = $crnl
    if (-not $fileText.Contains($crnl)) { $nl = [string][char]10 }
    $beginIdx = $fileText.IndexOf($script:BeginMarker)
    $endIdx = $fileText.IndexOf($script:EndMarker)
    if ($beginIdx -lt 0 -or $endIdx -lt 0) { throw 'generated-block markers not found in ' + $Path }
    if ($endIdx -lt $beginIdx) { throw 'generated-block markers out of order in ' + $Path }
    $lineEnd = $fileText.IndexOf($nl, $beginIdx)
    if ($lineEnd -lt 0) { throw 'generated-block region not delimited in ' + $Path }
    $regionStart = $lineEnd + $nl.Length
    $regionEnd = $fileText.LastIndexOf($nl, $endIdx)
    if ($regionEnd -lt $regionStart) { throw 'generated-block region not delimited in ' + $Path }
    $regionEnd += $nl.Length
    $region = $fileText.Substring($regionStart, $regionEnd - $regionStart)
    return [pscustomobject]@{
        Text = $region
        NewLine = $nl
        FileText = $fileText
    }
}

function Get-CommittedRows {
    param([string]$RegionText, [string]$NewLine)
    $raw = $RegionText.Split([string[]]@($NewLine), [System.StringSplitOptions]::None)
    $rows = [System.Collections.Generic.List[string]]::new()
    $limit = $raw.Count
    if ($limit -gt 0 -and [string]$raw[$limit - 1] -eq '') { $limit-- }
    for ($i = 0; $i -lt $limit; $i++) { [void]$rows.Add([string]$raw[$i]) }
    return $rows
}

function Copy-StringRange {
    param([string[]]$Source, [int]$Offset, [int]$Count)
    $out = [string[]]::new($Count)
    [Array]::Copy($Source, $Offset, $out, 0, $Count)
    return $out
}

$script:SelfTestFailures = 0

function Assert-True {
    param([bool]$Condition, [string]$Label)
    if ($Condition) { Write-Host ('    PASS: ' + $Label) }
    else {
        $script:SelfTestFailures++
        Write-Host ('    FAIL: ' + $Label)
    }
}

function Assert-Count {
    param([int]$Expected, [int]$Actual, [string]$Label)
    if ($Expected -eq $Actual) { Write-Host ('    PASS: ' + $Label) }
    else {
        $script:SelfTestFailures++
        Write-Host ('    FAIL: ' + $Label + ' (expected ' + $Expected + ', got ' + $Actual + ')')
    }
}

function Invoke-VerifyCommitted {
    $rows = @(Get-FallbackRows)
    $region = Get-CommittedRegion -Path $AppPath
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $expected = ($rows -join $region.NewLine) + $region.NewLine
    if ($region.Text -ceq $expected) {
        Write-Host ('  verify: pass (byte-for-byte, ' + $utf8.GetByteCount($expected) + ' bytes, ' + $rows.Count + ' rows)')
        return
    }
    Write-Host '  verify: FAIL'
    Write-Host ('    committed bytes: ' + $utf8.GetByteCount($region.Text) + ', regenerated bytes: ' + $utf8.GetByteCount($expected))
    $a = $region.Text.Split([string[]]@($region.NewLine), [System.StringSplitOptions]::None)
    $e = $expected.Split([string[]]@($region.NewLine), [System.StringSplitOptions]::None)
    $n = [Math]::Min($a.Count, $e.Count)
    for ($i = 0; $i -lt $n; $i++) {
        if ([string]$a[$i] -cne [string]$e[$i]) {
            Write-Host ('    first difference at row ' + ($i + 1) + ':')
            Write-Host ('      expected: [' + $e[$i] + ']')
            Write-Host ('      actual:   [' + $a[$i] + ']')
            break
        }
    }
    exit 1
}

function Invoke-SelfTest {
    $script:SelfTestFailures = 0
    Write-Host '  [1/2] synthetic fixture: extraction rules'

    $doc = [xml]$script:FixtureXaml
    Import-ResourceDictionary -Document $doc
    $rows = @(Get-FallbackRows)
    Assert-Count $script:ExpectedFixtureRows.Count $rows.Count 'fixture row count matches the expected sequence'
    $mismatch = -1
    $n = [Math]::Min($rows.Count, $script:ExpectedFixtureRows.Count)
    for ($i = 0; $i -lt $n; $i++) {
        if ([string]$rows[$i] -cne $script:ExpectedFixtureRows[$i]) { $mismatch = $i; break }
    }
    if ($mismatch -ge 0) {
        $script:SelfTestFailures++
        Write-Host ('    FAIL: fixture rows diverge at row ' + ($mismatch + 1))
        Write-Host ('      expected: [' + $script:ExpectedFixtureRows[$mismatch] + ']')
        Write-Host ('      actual:   [' + $rows[$mismatch] + ']')
    }

    Write-Host '  [2/2] committed block: structural invariants'
    $region = Get-CommittedRegion -Path $AppPath
    $committed = @(Get-CommittedRows -RegionText $region.Text -NewLine $region.NewLine)

    $ft = $region.FileText
    $nl = $region.NewLine
    $bi = $ft.IndexOf($script:BeginMarker)
    $ei = $ft.IndexOf($script:EndMarker)
    $blStart = $ft.LastIndexOf($nl, $bi) + $nl.Length
    $blEnd = $ft.IndexOf($nl, $bi)
    $beginLine = $ft.Substring($blStart, $blEnd - $blStart)
    $elStart = $ft.LastIndexOf($nl, $ei) + $nl.Length
    $elEnd = $ft.IndexOf($nl, $ei)
    $endLine = $ft.Substring($elStart, $elEnd - $elStart)
    Assert-True ($beginLine -cmatch ('^ {16}' + [regex]::Escape($script:BeginMarker) + '$')) 'BEGIN marker line uses the 16-space indent'
    Assert-True ($endLine -cmatch ('^ {16}' + [regex]::Escape($script:EndMarker) + '$')) 'END marker line uses the 16-space indent'

    Assert-Count $script:ExpectedRowCount $committed.Count 'committed row count matches the pinned dictionary size'

    $rowRegex = '^ {12}\("([^"]+)", "([^"]+)"\),$'
    $total = $committed.Count
    $parsedKeys = [string[]]::new($total)
    $parsedTokens = [string[]]::new($total)
    $badFormat = 0
    $uniqueKeys = [System.Collections.Generic.HashSet[string]]::new()
    for ($i = 0; $i -lt $total; $i++) {
        if ($committed[$i] -cmatch $rowRegex) {
            $parsedKeys[$i] = $Matches[1]
            $parsedTokens[$i] = $Matches[2]
            [void]$uniqueKeys.Add($parsedKeys[$i])
        } else {
            $badFormat++
        }
    }
    Assert-True ($badFormat -eq 0) 'every committed row uses the 12-space entry format'
    Assert-Count $total $uniqueKeys.Count 'committed keys are unique'

    $badPrefix = 0
    foreach ($tok in $parsedTokens) {
        if ($null -eq $tok -or $tok.Length -lt 2 -or -not ($script:AllowedPrefixes -contains $tok.Substring(0, 1)) -or $tok.Substring(1, 1) -ne ':') {
            $badPrefix++
        }
    }
    Assert-True ($badPrefix -eq 0) 'every committed token starts with a known type prefix'

    $split = -1
    for ($i = 0; $i -lt $total; $i++) {
        if ($parsedTokens[$i] -ceq $script:ElevationColor -or $parsedTokens[$i] -ceq $script:AcrylicColor) { $split = $i; break }
    }
    Assert-True ($split -gt 0) 'committed block contains an approximation run'
    if ($split -gt 0) {
        $sortedBase = Copy-StringRange $parsedKeys 0 $split
        [Array]::Sort($sortedBase, [System.StringComparer]::InvariantCultureIgnoreCase)
        $baseSorted = $true
        for ($i = 0; $i -lt $split; $i++) {
            if ($sortedBase[$i] -cne $parsedKeys[$i]) { $baseSorted = $false; break }
        }
        Assert-True $baseSorted 'base run is invariant-culture (case-insensitive) sorted'

        $approxCount = $total - $split
        $approxSorted = $true
        if ($approxCount -gt 0) {
            $sortedApprox = Copy-StringRange $parsedKeys $split $approxCount
            [Array]::Sort($sortedApprox, [System.StringComparer]::InvariantCultureIgnoreCase)
            for ($i = 0; $i -lt $approxCount; $i++) {
                if ($sortedApprox[$i] -cne $parsedKeys[$split + $i]) { $approxSorted = $false; break }
            }
        }
        Assert-True $approxSorted 'approximation run is invariant-culture (case-insensitive) sorted'

        $baseClean = $true
        for ($i = 0; $i -lt $split; $i++) {
            if ($parsedTokens[$i] -ceq $script:ElevationColor -or $parsedTokens[$i] -ceq $script:AcrylicColor) { $baseClean = $false; break }
        }
        Assert-True $baseClean 'no approximation token appears before the run boundary'

        $tailClean = $true
        for ($i = $split; $i -lt $total; $i++) {
            if ($parsedTokens[$i] -cne $script:ElevationColor -and $parsedTokens[$i] -cne $script:AcrylicColor) { $tailClean = $false; break }
        }
        Assert-True $tailClean 'every row after the run boundary uses an approximation token'
    }

    if ($script:SelfTestFailures -gt 0) {
        Write-Host ('  self-test FAILED: ' + $script:SelfTestFailures + ' assertion(s) failed')
        exit 1
    }
    Write-Host '  self-test: all assertions passed'
}

$script:FixtureXaml = @'
<ResourceDictionary
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <ResourceDictionary.ThemeDictionaries>
    <ResourceDictionary x:Key="Light">
      <SolidColorBrush x:Key="LightWins" Color="#112233" />
      <Color x:Key="DupKey">#AABBCC</Color>
      <Color x:Key="DupKey">#DDEEFF</Color>
    </ResourceDictionary>
    <ResourceDictionary x:Key="Dark">
      <SolidColorBrush x:Key="LightWins" Color="#445566" />
    </ResourceDictionary>
  </ResourceDictionary.ThemeDictionaries>
  <SolidColorBrush x:Key="RootFallback" Color="#778899" />
  <Color x:Key="DupKey">#FFFFFF00</Color>
  <LinearGradientBrush x:Key="ElevationBrush" />
  <StaticResource x:Key="AliasGradient" ResourceKey="ElevationBrush" />
  <AcrylicBrush x:Key="AcrylicBrushRes" />
  <StaticResource x:Key="AliasAcrylic" ResourceKey="AcrylicBrushRes" />
  <StaticResource x:Key="AliasDeep" ResourceKey="AliasAcrylic" />
  <DesktopAcrylicBackdrop x:Key="BackdropRes" />
  <StaticResource x:Key="AliasBackdrop" ResourceKey="BackdropRes" />
  <SolidColorBrush x:Key="LiteralAcrylic" Color="#F2FFFFFF" />
  <SolidColorBrush x:Key="ThemeRefBrush" Color="{ThemeResource SystemAccentColorDark1}" />
  <SolidColorBrush x:Key="Hex6Case" Color="#e81123" />
  <Color x:Key="Hex3">#0AF</Color>
  <Thickness x:Key="T1">24</Thickness>
  <Thickness x:Key="T2">2,4</Thickness>
  <Thickness x:Key="T3">2,4,6</Thickness>
  <Thickness x:Key="T4">1,2,3,4</Thickness>
  <CornerRadius x:Key="R1">16</CornerRadius>
  <CornerRadius x:Key="R2">1.5</CornerRadius>
  <CornerRadius x:Key="R3">-2</CornerRadius>
  <CornerRadius x:Key="R4">1,2,3,4</CornerRadius>
  <Double x:Key="D1">0.5</Double>
  <Boolean x:Key="B1">True</Boolean>
  <String x:Key="S1">hello</String>
  <String x:Key="S2"> pad </String>
  <GridLength x:Key="G1">Auto</GridLength>
  <FontWeight x:Key="W1">Bold</FontWeight>
  <HorizontalAlignment x:Key="H1">Left</HorizontalAlignment>
  <VerticalAlignment x:Key="A1">Top</VerticalAlignment>
  <Visibility x:Key="V1">Collapsed</Visibility>
  <FontFamily x:Key="F1">Segoe UI</FontFamily>
  <Style x:Key="Y1" TargetType="Button" />
  <Int32 x:Key="SkipInt">5</Int32>
  <MyTestConverter x:Key="SkipConv" />
  <MyCustomThing x:Key="E1">Overlay</MyCustomThing>
  <StaticResource x:Key="AliasToInt" ResourceKey="SkipInt" />
</ResourceDictionary>
'@

$script:ExpectedFixtureRows = @(
    '            ("A1", "A:Top"),'
    '            ("B1", "B:True"),'
    '            ("D1", "D:0.5"),'
    '            ("DupKey", "C:#FFAABBCC"),'
    '            ("E1", "E:MyCustomThing:Overlay"),'
    '            ("F1", "F:Segoe UI"),'
    '            ("G1", "G:Auto"),'
    '            ("H1", "H:Left"),'
    '            ("Hex3", "C:#FF00AAFF"),'
    '            ("Hex6Case", "C:#FFe81123"),'
    '            ("LightWins", "C:#FF112233"),'
    '            ("R1", "R:1,1,1,1"),'
    '            ("R2", "R:1,1,1,1"),'
    '            ("R3", "R:-,-,-,-"),'
    '            ("R4", "R:1,2,3,4"),'
    '            ("RootFallback", "C:#FF778899"),'
    '            ("S1", "S:hello"),'
    '            ("S2", "S: pad "),'
    '            ("T1", "T:2,2,2,2"),'
    '            ("T2", "T:,,,"),'
    '            ("T3", "T:,,,"),'
    '            ("T4", "T:1,2,3,4"),'
    '            ("ThemeRefBrush", "C:{ThemeResource SystemAccentColorDark1}"),'
    '            ("V1", "V:Collapsed"),'
    '            ("W1", "W:Bold"),'
    '            ("Y1", "Y:Button"),'
    '            ("AliasAcrylic", "C:#F2FFFFFF"),'
    '            ("AliasBackdrop", "C:#F2FFFFFF"),'
    '            ("AliasDeep", "C:#F2FFFFFF"),'
    '            ("AliasGradient", "C:#14000000"),'
    '            ("LiteralAcrylic", "C:#F2FFFFFF"),'
)

if ($SelfTest) {
    Invoke-SelfTest
}

if ($VerifyCommitted) {
    if (-not $XamlPath) { $XamlPath = Resolve-DefaultXamlPath }
    Write-Host ('  source: ' + $XamlPath)
    Write-Host ('  sha256: ' + (Get-FileHash -LiteralPath $XamlPath -Algorithm SHA256).Hash)
    Import-XamlFile -Path $XamlPath
    Invoke-VerifyCommitted
}

if (-not $SelfTest -and -not $VerifyCommitted) {
    if (-not $XamlPath) { $XamlPath = Resolve-DefaultXamlPath }
    Import-XamlFile -Path $XamlPath
    $rows = Get-FallbackRows
    Write-Host ('  source: ' + $XamlPath)
    Write-Host ('  sha256: ' + (Get-FileHash -LiteralPath $XamlPath -Algorithm SHA256).Hash)
    Write-Host ('  rows: ' + $rows.Count)
    Write-Output $rows
}
