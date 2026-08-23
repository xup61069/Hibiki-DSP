[CmdletBinding()]
param(
  [int]$Issue,
  [switch]$NoSource,
  [switch]$SelfTest
)

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

function Get-ContextExistingAttributes {
  param(
    [Parameter(Mandatory)][string]$Path,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  if ($null -ne $SyntheticAttributes -and $SyntheticAttributes.ContainsKey($fullPath)) {
    return [System.IO.FileAttributes]$SyntheticAttributes[$fullPath]
  }
  if ($null -ne $SyntheticInspectionErrors -and $SyntheticInspectionErrors.ContainsKey($fullPath)) {
    throw "Context path inspection failed: $fullPath ($($SyntheticInspectionErrors[$fullPath]))"
  }
  try {
    return [System.IO.FileAttributes](Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop).Attributes
  }
  catch {
    if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { return $null }
    throw "Context path inspection failed: $fullPath ($($_.Exception.Message))"
  }
}

function Assert-ContextPathUnderRoot {
  param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$RepositoryRoot,
    [hashtable]$SyntheticAttributes,
    [hashtable]$SyntheticInspectionErrors
  )

  $expectedRoot = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
  $candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')

  $cursor = $candidate
  while ($true) {
    $attributes = Get-ContextExistingAttributes -Path $cursor -SyntheticAttributes $SyntheticAttributes -SyntheticInspectionErrors $SyntheticInspectionErrors
    if ($null -ne $attributes) {
      if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Context path or parent is a reparse point: $cursor"
      }
      if ($cursor -ne $candidate -and (($attributes -band [System.IO.FileAttributes]::Directory) -eq 0)) {
        throw "Context path parent is not a directory: $cursor"
      }
    }

    if ($cursor -eq $expectedRoot) { break }
    if ($cursor -match '^[A-Za-z]:\\?$') {
      throw "Context path escapes the repository root: $candidate"
    }
    $rawParent = Split-Path -Parent $cursor -ErrorAction Stop
    $parent = [IO.Path]::GetFullPath($rawParent).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $cursor) {
      throw "Context path escapes the repository root: $candidate"
    }
    $cursor = $parent
  }
}

$script:ContextSyntheticAttributes = $null
$script:ContextSyntheticInspectionErrors = $null

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

  $archive = [int][System.IO.FileAttributes]::Archive
  $directory = [int][System.IO.FileAttributes]::Directory
  $reparsePoint = [int][System.IO.FileAttributes]::ReparsePoint
  $repoFull = [IO.Path]::GetFullPath($repo).TrimEnd('\', '/')

  function New-GuardSyntheticChain([string]$Root, [string]$RelativeLeaf, [int]$LeafAttributes) {
    $fullLeaf = [IO.Path]::GetFullPath((Join-Path $Root $RelativeLeaf)).TrimEnd('\', '/')
    $map = @{}
    $cursor = $fullLeaf
    while ($true) {
      $map[$cursor] = if ($cursor -eq $fullLeaf) { $LeafAttributes } else { $directory }
      $rawParent = Split-Path -Parent $cursor -ErrorAction SilentlyContinue
      if ([string]::IsNullOrWhiteSpace($rawParent)) { break }
      $parent = [IO.Path]::GetFullPath($rawParent).TrimEnd('\', '/')
      if ($parent -match '^[A-Za-z]:$') { break }
      if ($parent -eq $cursor) { break }
      $cursor = $parent
    }
    return @{ Leaf = $fullLeaf; Attributes = $map }
  }

  function Invoke-GuardCase([scriptblock]$Action) {
    try {
      & $Action
      return $null
    } catch {
      return "$($_.Exception.Message) [$($_.InvocationInfo.ScriptLineNumber)]"
    }
  }

  $guardCases = @(
    @{ Name = 'accept-inside-repository'; ExpectedError = $null; Action = {
      $chain = New-GuardSyntheticChain -Root $repoFull -RelativeLeaf 'docs/sub/target.md' -LeafAttributes $archive
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $chain.Attributes
    } },
    @{ Name = 'reject-outside-repository'; ExpectedError = 'escapes the repository root'; Action = {
      $outsideRoot = Join-Path ([IO.Path]::GetTempPath()) 'hibiki-context-pack-outside'
      $chain = New-GuardSyntheticChain -Root $outsideRoot -RelativeLeaf 'secret.txt' -LeafAttributes $archive
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $chain.Attributes
    } },
    @{ Name = 'reject-reparse-point-leaf'; ExpectedError = 'reparse point'; Action = {
      $chain = New-GuardSyntheticChain -Root $repoFull -RelativeLeaf 'docs/link.md' -LeafAttributes ($archive -bor $reparsePoint)
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $chain.Attributes
    } },
    @{ Name = 'reject-reparse-point-parent'; ExpectedError = 'reparse point'; Action = {
      $chain = New-GuardSyntheticChain -Root $repoFull -RelativeLeaf 'docs/sub/target.md' -LeafAttributes $archive
       $middle = [IO.Path]::GetFullPath((Join-Path $repoFull 'docs\sub')).TrimEnd('\', '/')
      $chain.Attributes[$middle] = $directory -bor $reparsePoint
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $chain.Attributes
    } },
    @{ Name = 'reject-parent-not-directory'; ExpectedError = 'not a directory'; Action = {
      $chain = New-GuardSyntheticChain -Root $repoFull -RelativeLeaf 'docs/sub/target.md' -LeafAttributes $archive
      $middle = [IO.Path]::GetFullPath((Join-Path $repoFull 'docs\sub')).TrimEnd('\', '/')
      $chain.Attributes[$middle] = $archive
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $chain.Attributes
    } },
    @{ Name = 'reject-inspection-error'; ExpectedError = 'Context path inspection failed'; Action = {
      $chain = New-GuardSyntheticChain -Root $repoFull -RelativeLeaf 'docs/sub/target.md' -LeafAttributes $archive
      Assert-ContextPathUnderRoot -Path $chain.Leaf -RepositoryRoot $repoFull -SyntheticAttributes $null -SyntheticInspectionErrors @{ $chain.Leaf = 'access denied' }
    } }
  )

  foreach ($guardCase in $guardCases) {
    $actualError = Invoke-GuardCase $guardCase.Action
    if ($null -eq $guardCase.ExpectedError) {
      if ($null -ne $actualError) {
        throw "context-pack guard self-test case '$($guardCase.Name)' expected success but got: $actualError"
      }
    } elseif ($null -eq $actualError -or -not $actualError.Contains($guardCase.ExpectedError)) {
      throw "context-pack guard self-test case '$($guardCase.Name)' expected error containing '$($guardCase.ExpectedError)' but got: $actualError"
    }
  }

  Write-Output "Context-pack path-guard self-test passed ($($guardCases.Count) cases)."
  exit 0
}

if (-not $PSBoundParameters.ContainsKey('Issue')) {
  throw "No Issue specified. Pass -Issue <number>."
}

$ghOutput = & gh issue view $Issue --json body --jq '.body' 2>$null
if ($LASTEXITCODE -ne 0 -or -not $ghOutput) {
  throw "Cannot read Issue $Issue via gh. Run 'gh issue view $Issue' manually and confirm the handoff block."
}
$handoffText = $ghOutput | Out-String
$specIds = @([regex]::Matches($handoffText, 'SPEC-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$adrIds = @([regex]::Matches($handoffText, 'ADR-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$seenFiles = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
$sourceGlobs = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)

function Write-ContextFile([string]$label, [string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { throw "Context file missing: $path" }
  $resolved = (Resolve-Path -LiteralPath $path).Path
  Assert-ContextPathUnderRoot -Path $resolved -RepositoryRoot $repo -SyntheticAttributes $script:ContextSyntheticAttributes -SyntheticInspectionErrors $script:ContextSyntheticInspectionErrors
  if (-not $seenFiles.Add($resolved)) { return }
  Write-Output "=== $label :: $path ==="
  Get-Content -LiteralPath $path
}

Write-Output "=== Hibiki context pack: Issue #$Issue ==="
Write-ContextFile 'RULES' (Join-Path $repo 'AGENTS.md')
Write-ContextFile 'MULTI_AGENT' (Join-Path $repo 'docs/ai/MULTI_AGENT.md')
Write-ContextFile 'START' (Join-Path $repo 'docs/START_HERE.md')
Write-ContextFile 'MAP' (Join-Path $repo 'docs/PROJECT_MAP.md')
Write-ContextFile 'BASELINE' (Join-Path $repo 'docs/state/BASELINE.md')
Write-Output '=== ISSUE BODY (handoff source) ==='
Write-Output $handoffText

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
  $file = Get-ChildItem -LiteralPath (Join-Path $repo 'docs/adr') -Filter "*-*.md" -File |
    Where-Object { Select-String -LiteralPath $_.FullName -Pattern "ADR-$($id.Substring(4))" -Quiet } |
    Select-Object -First 1
  if ($file) { Write-ContextFile $id $file.FullName }
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
