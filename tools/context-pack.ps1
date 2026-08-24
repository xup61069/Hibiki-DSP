#Requires -Version 7
[CmdletBinding()]
param(
  [int]$Issue,
  [switch]$NoSource,
  [switch]$IncludeRepositoryState,
  [ValidateRange(4096, 1000000)][int]$MaxCharacters = 48000,
  [ValidateRange(1024, 250000)][int]$MaxEstimatedTokens = 12000,
  [switch]$SelfTest
)

Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot

function Convert-ContextGlobToRegex([string]$glob) {
  $normalized = $glob.Replace('\', '/')
  $builder = [Text.StringBuilder]::new()
  for ($index = 0; $index -lt $normalized.Length; $index++) {
    $character = $normalized[$index]
    if ($character -eq '*') {
      if ($index + 1 -lt $normalized.Length -and $normalized[$index + 1] -eq '*') {
        $index++
        if ($index + 1 -lt $normalized.Length -and $normalized[$index + 1] -eq '/') {
          $index++
          [void]$builder.Append('(?:.*/)?')
        }
        else {
          [void]$builder.Append('.*')
        }
      }
      else {
        [void]$builder.Append('[^/]*')
      }
    }
    elseif ($character -eq '?') {
      [void]$builder.Append('[^/]')
    }
    else {
      [void]$builder.Append([regex]::Escape([string]$character))
    }
  }
  return "^$($builder.ToString())$"
}

function Test-ContextGlob([string]$relativePath, [string]$glob) {
  return $relativePath -match (Convert-ContextGlobToRegex $glob)
}

function Get-ContextPackExistingAttributes {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $resolvedPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($resolvedPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$resolvedPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($resolvedPath)) {
    throw "Context pack path inspection failed: $resolvedPath ($($SyntheticInspectionErrors[$resolvedPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $resolvedPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Context pack path inspection failed: $resolvedPath ($($_.Exception.Message))"
  }
}

function Resolve-ContextPackPath {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root,
    [ValidateSet('File', 'Directory')][string]$Kind = 'File',
    [switch]$AllowMissingLeaf,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  $resolvedPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if (-not $resolvedPath.StartsWith(
        $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Context path must remain under the repository root: $resolvedPath"
  }

  $cursor = $resolvedPath
  while ($true) {
    $attributes = Get-ContextPackExistingAttributes -Path $cursor -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Context path or parent is a reparse point: $cursor"
      }
      if ($cursor -eq $resolvedRoot) {
        if (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
          throw "Context path root is not a directory: $cursor"
        }
      } elseif ($cursor -eq $resolvedPath) {
        if ($Kind -eq 'File' -and (($attributes -band [System.IO.FileAttributes]::Directory) -ne 0)) {
          throw "Context path is not a file: $cursor"
        }
      } elseif (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0) {
        throw "Context path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $resolvedRoot) { break }
    $parent = [System.IO.Path]::GetFullPath((Split-Path -Parent $cursor)).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Context path could not reach the repository root: $resolvedPath"
    }
    $cursor = $parent
  }

  $leafAttributes = Get-ContextPackExistingAttributes -Path $resolvedPath -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
  if ($null -eq $leafAttributes) {
    if (-not $AllowMissingLeaf) { throw "Context path does not exist: $resolvedPath" }
    return $null
  }
  return $resolvedPath
}

$contextSections = [System.Collections.Generic.List[string]]::new()
$contextCharacters = 0

function ConvertTo-ContextNewlines([string]$Text) {
  $lf = $Text -replace "\r\n?", "`n"
  return $lf.Replace("`n", [Environment]::NewLine)
}

function Get-ConservativeContextTokenEstimate([string]$Text) {
  # Offline heuristic for ordinary mixed Chinese/code context: ASCII is charged
  # at one token per four characters and every non-ASCII UTF-16 code unit at one.
  # It is deliberately cautious for CJK-heavy packs, but is not an exact model
  # tokenizer or a guaranteed mathematical upper bound.
  $asciiCharacters = 0
  $nonAsciiCharacters = 0
  foreach ($character in $Text.ToCharArray()) {
    if ([int][char]$character -le 127) { $asciiCharacters++ }
    else { $nonAsciiCharacters++ }
  }
  return [int]([Math]::Ceiling($asciiCharacters / 4.0) + $nonAsciiCharacters)
}

function Add-ContextSection {
  param(
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$Source,
    [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content,
    [int]$Limit = $MaxCharacters
  )

  $heading = if ([string]::IsNullOrWhiteSpace($Source)) {
    "=== $Label ==="
  } else {
    "=== $Label :: $Source ==="
  }
  $normalizedContent = ConvertTo-ContextNewlines $Content
  $section = ($heading + [Environment]::NewLine + $normalizedContent.TrimEnd())
  $separatorCharacters = if ($script:contextSections.Count -eq 0) { 0 } else { 2 }
  $nextCharacters = $script:contextCharacters + $separatorCharacters + $section.Length
  if ($nextCharacters -gt $Limit) {
    throw "Context pack would exceed the $Limit character budget before output (next section: $Label). Use -NoSource, narrow the Issue Spec/ADR references, inspect files locally, or explicitly raise -MaxCharacters for a bounded audit."
  }

  [void]$script:contextSections.Add($section)
  $script:contextCharacters = $nextCharacters
}

function Get-ContextPackText {
  return ($script:contextSections -join ([Environment]::NewLine + [Environment]::NewLine))
}

function New-ContextPackOutput {
  param(
    [Parameter(Mandatory = $true)][int]$CharacterLimit,
    [Parameter(Mandatory = $true)][int]$EstimatedTokenLimit,
    [bool]$RepositoryStateIncluded = $false
  )

  $contentText = Get-ContextPackText
  $packCharacters = 0
  $estimatedTokens = 0
  $candidate = ''
  $stable = $false
  for ($iteration = 0; $iteration -lt 8; $iteration++) {
    $summary = @(
      '=== PACK SUMMARY ==='
      "content_characters: $contextCharacters"
      "pack_characters: $packCharacters"
      "estimated_tokens: $estimatedTokens"
      "budget_characters: $CharacterLimit"
      "budget_estimated_tokens: $EstimatedTokenLimit"
      "repository_state_included: $($RepositoryStateIncluded.ToString().ToLowerInvariant())"
    ) -join [Environment]::NewLine
    $candidate = $contentText + [Environment]::NewLine + $summary + [Environment]::NewLine
    $nextCharacters = $candidate.Length
    $nextEstimatedTokens = Get-ConservativeContextTokenEstimate $candidate
    if ($nextCharacters -eq $packCharacters -and $nextEstimatedTokens -eq $estimatedTokens) {
      $stable = $true
      break
    }
    $packCharacters = $nextCharacters
    $estimatedTokens = $nextEstimatedTokens
  }
  if (-not $stable) { throw 'Context pack summary accounting did not converge.' }
  if ($packCharacters -gt $CharacterLimit) {
    throw "Context pack would exceed the $CharacterLimit character budget before output (complete serialized pack: $packCharacters). Use -NoSource, narrow the Issue Spec/ADR references, inspect files locally, or explicitly raise both budgets for a bounded audit."
  }
  if ($estimatedTokens -gt $EstimatedTokenLimit) {
    throw "Context pack would exceed the conservative $EstimatedTokenLimit estimated-token budget before output (complete serialized pack estimate: $estimatedTokens). Use -NoSource, narrow the Issue Spec/ADR references, inspect files locally, or explicitly raise both budgets for a bounded audit."
  }
  return $candidate
}

if ($SelfTest) {
  $cases = @(
    @{ Name = 'exact-match'; Path = 'docs/AI_HANDOFF.md'; Glob = 'docs/AI_HANDOFF.md'; Expected = $true },
    @{ Name = 'exact-near-miss'; Path = 'docs/AI_HANDOFF.txt'; Glob = 'docs/AI_HANDOFF.md'; Expected = $false },
    @{ Name = 'single-star-file'; Path = 'tools/context-pack.ps1'; Glob = 'tools/*.ps1'; Expected = $true },
    @{ Name = 'single-star-no-directory-crossing'; Path = 'tools/nested/context-pack.ps1'; Glob = 'tools/*.ps1'; Expected = $false },
    @{ Name = 'single-question-mark'; Path = 'src/a.cpp'; Glob = 'src/?.cpp'; Expected = $true },
    @{ Name = 'recursive-direct-child'; Path = 'src/foo.cpp'; Glob = 'src/**/*.cpp'; Expected = $true },
    @{ Name = 'recursive-deep-child'; Path = 'src/hub/foo.cpp'; Glob = 'src/**/*.cpp'; Expected = $true },
    @{ Name = 'recursive-extension-near-miss'; Path = 'src/foo.c'; Glob = 'src/**/*.cpp'; Expected = $false },
    @{ Name = 'recursive-suffix-direct-child'; Path = 'src/hub/output_group.cpp'; Glob = 'src/hub/**output*'; Expected = $true },
    @{ Name = 'recursive-suffix-deep-child'; Path = 'src/hub/wasapi/output_group.cpp'; Glob = 'src/hub/**output*'; Expected = $true },
    @{ Name = 'recursive-root-direct-child'; Path = 'evidence/initial.json'; Glob = 'evidence/**'; Expected = $true },
    @{ Name = 'recursive-root-deep-child'; Path = 'evidence/0000-foundation/initial.json'; Glob = 'evidence/**'; Expected = $true }
  )

  foreach ($case in $cases) {
    $actual = Test-ContextGlob -relativePath $case.Path -glob $case.Glob
    if ($actual -ne $case.Expected) {
      throw "context-pack self-test case '$($case.Name)' expected $($case.Expected) but got $actual for '$($case.Path)' against '$($case.Glob)'."
    }
  }

  Write-Output "Context-pack glob self-test passed ($($cases.Count) cases)."

  $pathRepositoryRoot = [System.IO.Path]::GetFullPath('C:\hibiki-context-pack-selftest').TrimEnd('\', '/')
  $pathDocsRoot = Join-Path $pathRepositoryRoot 'docs'
  $pathInsideFile = Join-Path $pathDocsRoot 'AI_HANDOFF.md'
  $pathOutsideFile = [System.IO.Path]::GetFullPath('C:\hibiki-context-pack-selftest-outside\secret.md')
  $directoryAttribute = [System.IO.FileAttributes]::Directory
  $fileAttribute = [System.IO.FileAttributes]::Archive
  $pathCases = 0

  $validResolved = Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
    $pathRepositoryRoot = $directoryAttribute
    $pathDocsRoot = $directoryAttribute
    $pathInsideFile = $fileAttribute
  }
  if ($validResolved -ne $pathInsideFile) { throw 'context-pack self-test expected a valid in-repository path to resolve.' }
  $pathCases++

  if ($null -eq (Resolve-ContextPackPath -Path (Join-Path $repo 'AGENTS.md') -Root $repo -Kind File)) {
    throw 'context-pack self-test expected the real AGENTS.md file to resolve.'
  }
  $pathCases++

  $outsideCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathOutsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{}
  } catch { $outsideCaught = $_.Exception.Message -match 'must remain under the repository root' }
  if (-not $outsideCaught) { throw 'context-pack self-test expected an outside-root rejection.' }
  $pathCases++

  $reparseLeafCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathDocsRoot = $directoryAttribute
      $pathInsideFile = $fileAttribute -bor [System.IO.FileAttributes]::ReparsePoint
    }
  } catch { $reparseLeafCaught = $_.Exception.Message -match 'is a reparse point' }
  if (-not $reparseLeafCaught) { throw 'context-pack self-test expected a reparse-leaf rejection.' }
  $pathCases++

  $reparseParentCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathDocsRoot = $directoryAttribute -bor [System.IO.FileAttributes]::ReparsePoint
      $pathInsideFile = $fileAttribute
    }
  } catch { $reparseParentCaught = $_.Exception.Message -match 'is a reparse point' }
  if (-not $reparseParentCaught) { throw 'context-pack self-test expected a reparse-parent rejection.' }
  $pathCases++

  $nonFileCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathDocsRoot = $directoryAttribute
      $pathInsideFile = $directoryAttribute
    }
  } catch { $nonFileCaught = $_.Exception.Message -match 'is not a file' }
  if (-not $nonFileCaught) { throw 'context-pack self-test expected a non-file leaf rejection.' }
  $pathCases++

  $missingAllowedResolved = Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -AllowMissingLeaf -SyntheticAttributes @{
    $pathRepositoryRoot = $directoryAttribute
    $pathDocsRoot = $directoryAttribute
  }
  if ($null -ne $missingAllowedResolved) { throw 'context-pack self-test expected a missing leaf to stay unresolved.' }
  $pathCases++

  $missingStrictCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathDocsRoot = $directoryAttribute
    }
  } catch { $missingStrictCaught = $_.Exception.Message -match 'does not exist' }
  if (-not $missingStrictCaught) { throw 'context-pack self-test expected a missing-path rejection.' }
  $pathCases++

  $inspectionLeafCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathDocsRoot = $directoryAttribute
    } -SyntheticInspectionErrors @{ $pathInsideFile = 'synthetic access denied' }
  } catch { $inspectionLeafCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $inspectionLeafCaught) { throw 'context-pack self-test expected a leaf inspection-failure rejection.' }
  $pathCases++

  $inspectionParentCaught = $false
  try {
    Resolve-ContextPackPath -Path $pathInsideFile -Root $pathRepositoryRoot -Kind File -SyntheticAttributes @{
      $pathRepositoryRoot = $directoryAttribute
      $pathInsideFile = $fileAttribute
    } -SyntheticInspectionErrors @{ $pathDocsRoot = 'synthetic sharing violation' }
  } catch { $inspectionParentCaught = $_.Exception.Message -match 'path inspection failed' }
  if (-not $inspectionParentCaught) { throw 'context-pack self-test expected a parent inspection-failure rejection.' }
  $pathCases++

  Write-Output "Context-pack path guard self-test passed ($pathCases cases)."

  $contextSections.Clear()
  $contextCharacters = 0
  Add-ContextSection -Label 'BUDGET/ONE' -Content '1234567890' -Limit 64
  $budgetCaught = $false
  try {
    Add-ContextSection -Label 'BUDGET/TWO' -Content ('x' * 64) -Limit 64
  } catch { $budgetCaught = $_.Exception.Message -match 'before output' }
  if (-not $budgetCaught -or $contextSections.Count -ne 1) {
    throw 'context-pack self-test expected an oversized section to fail before partial output.'
  }

  $contextSections.Clear()
  $contextCharacters = 0
  Add-ContextSection -Label 'FINAL' -Content 'bounded' -Limit 512
  $boundedOutput = New-ContextPackOutput -CharacterLimit 512 -EstimatedTokenLimit 512
  if ($boundedOutput.Length -gt 512 -or $boundedOutput -notmatch "pack_characters: $($boundedOutput.Length)") {
    throw 'context-pack self-test expected exact complete-output character accounting.'
  }

  $finalOverheadCaught = $false
  try {
    $null = New-ContextPackOutput -CharacterLimit 64 -EstimatedTokenLimit 512
  } catch { $finalOverheadCaught = $_.Exception.Message -match 'complete serialized pack' }
  if (-not $finalOverheadCaught) {
    throw 'context-pack self-test expected final summary overhead to be included in the character budget.'
  }

  $contextSections.Clear()
  $contextCharacters = 0
  Add-ContextSection -Label 'TOKEN' -Content ('界' * 64) -Limit 4096
  $tokenBudgetCaught = $false
  try {
    $null = New-ContextPackOutput -CharacterLimit 4096 -EstimatedTokenLimit 32
  } catch { $tokenBudgetCaught = $_.Exception.Message -match 'estimated-token budget' }
  if (-not $tokenBudgetCaught) {
    throw 'context-pack self-test expected mixed-language token pressure to fail closed.'
  }
  Write-Output 'Context-pack output budget self-test passed (section, final serialized, and estimated-token limits).'
  $foundationOutput = & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'context-pack.ps1') -Issue 0 -NoSource 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw 'context-pack self-test expected -Issue 0 foundation bootstrap to exit successfully.'
  }
  if ((($foundationOutput | Out-String)) -notmatch 'Hibiki task context: Issue #0') {
    throw 'context-pack self-test expected the foundation bootstrap pack header.'
  }
  Write-Output 'Context-pack foundation bootstrap self-test passed (-Issue 0 builds without a GitHub issue).'
  exit 0
}

if (-not $PSBoundParameters.ContainsKey('Issue')) {
  throw "No Issue specified. Pass -Issue <number>."
}
if ($IncludeRepositoryState -and
    (-not $PSBoundParameters.ContainsKey('MaxCharacters') -or
     -not $PSBoundParameters.ContainsKey('MaxEstimatedTokens'))) {
  throw 'IncludeRepositoryState requires explicit -MaxCharacters and -MaxEstimatedTokens values.'
}

if ($Issue -eq 0) {
  # Foundation bootstrap pack (Issue 0) has no GitHub backing; it intentionally
  # aggregates repository entry rules and tests/* bootstrap source files.
  $handoffText = ''
} else {
  $ghOutput = & gh issue view $Issue --json body --jq '.body' 2>$null
  if ($LASTEXITCODE -ne 0 -or -not $ghOutput) {
    throw "Cannot read Issue $Issue via gh. Run 'gh issue view $Issue' manually and confirm the handoff block."
  }
  $handoffText = $ghOutput | Out-String
}
$specIds = @([regex]::Matches($handoffText, 'SPEC-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$adrIds = @([regex]::Matches($handoffText, 'ADR-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$seenFiles = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
$sourceGlobs = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)

function Write-ContextFile([string]$label, [string]$path) {
  $resolved = Resolve-ContextPackPath -Path $path -Root $repo -Kind File -AllowMissingLeaf
  if ($null -eq $resolved) { throw "Context file missing: $path" }
  if (-not $seenFiles.Add($resolved)) { return }
  Add-ContextSection -Label $label -Source $path -Content (Get-Content -LiteralPath $resolved -Raw)
}

Add-ContextSection -Label "Hibiki task context: Issue #$Issue" -Content @'
Preflight contract: read AGENTS.md and docs/START_HERE.md once from the checked-out repository,
then treat the active Issue handoff, referenced Spec/ADR, source, tests and evidence as task truth.
This pack intentionally does not replay global rules or repository snapshots by default.
'@

if ($IncludeRepositoryState) {
  Write-ContextFile 'RULES' (Join-Path $repo 'AGENTS.md')
  Write-ContextFile 'START' (Join-Path $repo 'docs/START_HERE.md')
  Write-ContextFile 'MULTI_AGENT' (Join-Path $repo 'docs/ai/MULTI_AGENT.md')
  Write-ContextFile 'AI_HANDOFF' (Join-Path $repo 'docs/AI_HANDOFF.md')
  Write-ContextFile 'MAP' (Join-Path $repo 'docs/PROJECT_MAP.md')
  Write-ContextFile 'BASELINE' (Join-Path $repo 'docs/state/BASELINE.md')
}

if ($Issue -ne 0) {
  Add-ContextSection -Label 'ISSUE BODY (handoff source)' -Content $handoffText
}

foreach ($id in $specIds) {
  $file = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/specs') -Filter "$id-*.md" -File | Select-Object -First 1
  if ($file) {
    Write-ContextFile $id $file.FullName
    $specText = Get-Content -LiteralPath $file.FullName -Raw
    $globMatch = [regex]::Match($specText, 'source_globs:\s*\[(?<body>[^\]]*)\]')
    if ($globMatch.Success) {
      foreach ($token in ($globMatch.Groups['body'].Value -split ',')) {
        $glob = $token.Trim().Trim('"', "'")
        if ($glob) { [void]$sourceGlobs.Add($glob) }
      }
    }
  }
}
foreach ($id in $adrIds) {
  $adrFiles = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/adr') -Filter "*-*.md" -File
  foreach ($file in $adrFiles) {
    Resolve-ContextPackPath -Path $file.FullName -Root $repo -Kind File | Out-Null
    if (Select-String -LiteralPath $file.FullName -Pattern "ADR-$($id.Substring(4))" -Quiet) {
      Write-ContextFile $id $file.FullName
      break
    }
  }
}

if (-not $NoSource) {
  $allowedExtensions = @(
    '.h', '.hpp', '.c', '.cc', '.cpp', '.cxx', '.cs', '.xaml', '.ps1', '.psm1',
    '.json', '.yml', '.yaml', '.inf', '.rc', '.idl', '.def', '.cmake', '.txt'
  )
  $roots = @('src', 'asio', 'vst-host', 'driver', 'sdk', 'apps', 'extensions',
             'installer', 'tools', 'schemas', 'config', '.github')
  foreach ($root in $roots) {
    $rootPath = Join-Path $repo $root
    if (-not (Test-Path $rootPath)) { continue }
    Get-ChildItem -LiteralPath $rootPath -Recurse -File |
      Where-Object { $_.Extension.ToLowerInvariant() -in $allowedExtensions } |
      ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($repo, $_.FullName).Replace('\', '/')
        $matchesSpec = $false
        foreach ($glob in $sourceGlobs) {
          if (Test-ContextGlob $relative $glob) {
            $matchesSpec = $true
            break
          }
        }
        # Foundation Issue 0 is intentionally a whole-repository bootstrap;
        # later Issues stay selective through their Spec source_globs.
        if ($Issue -eq 0 -and ($relative -like 'tests/*')) { $matchesSpec = $true }
        if ($matchesSpec) { Write-ContextFile "SOURCE/$root" $_.FullName }
      }
  }
}

$evidenceRoot = Join-Path $repo "evidence/$Issue"
if (Test-Path $evidenceRoot) {
  Get-ChildItem -LiteralPath $evidenceRoot -Filter '*.json' -File |
    Sort-Object Name |
    ForEach-Object { Write-ContextFile 'EVIDENCE' $_.FullName }
}

$packOutput = New-ContextPackOutput -CharacterLimit $MaxCharacters `
  -EstimatedTokenLimit $MaxEstimatedTokens `
  -RepositoryStateIncluded $IncludeRepositoryState.IsPresent
[Console]::Out.Write($packOutput)
