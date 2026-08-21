[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][int]$Issue,
  [switch]$NoSource
)

$repo = Split-Path -Parent $PSScriptRoot
$handoff = Join-Path $repo "docs/tasks/active/$Issue.md"
if (-not (Test-Path $handoff)) {
  throw "No handoff exists for Issue $Issue. Create docs/tasks/active/$Issue.md before editing."
}

$handoffText = Get-Content -LiteralPath $handoff -Raw
$specIds = @([regex]::Matches($handoffText, 'SPEC-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$adrIds = @([regex]::Matches($handoffText, 'ADR-[0-9]{4}') | ForEach-Object Value | Sort-Object -Unique)
$seenFiles = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
$sourceGlobs = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)

function Write-ContextFile([string]$label, [string]$path) {
  if (-not (Test-Path $path)) { throw "Context file missing: $path" }
  $resolved = (Resolve-Path -LiteralPath $path).Path
  if (-not $seenFiles.Add($resolved)) { return }
  Write-Output "=== $label :: $path ==="
  Get-Content -LiteralPath $path
}

function Convert-ContextGlobToRegex([string]$glob) {
  $pattern = [regex]::Escape($glob.Replace('\', '/'))
  $pattern = $pattern.Replace('\*\*', '.*')
  $pattern = $pattern.Replace('\*', '[^/]*')
  $pattern = $pattern.Replace('\?', '[^/]')
  return "^$pattern$"
}

function Test-ContextGlob([string]$relativePath, [string]$glob) {
  return $relativePath -match (Convert-ContextGlobToRegex $glob)
}

Write-Output "=== Hibiki context pack: Issue #$Issue ==="
Write-ContextFile 'RULES' (Join-Path $repo 'AGENTS.md')
Write-ContextFile 'START' (Join-Path $repo 'docs/START_HERE.md')
Write-ContextFile 'MAP' (Join-Path $repo 'docs/PROJECT_MAP.md')
Write-ContextFile 'BASELINE' (Join-Path $repo 'docs/state/BASELINE.md')
Write-Output '=== HANDOFF ==='
Get-Content -LiteralPath $handoff

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
