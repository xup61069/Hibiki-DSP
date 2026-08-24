<#
.SYNOPSIS
  Commit message integrity gate for Hibiki DSP.

.DESCRIPTION
  Detects common AI-generated commit message corruption:
    1. U+FFFD replacement characters (mojibake / encoding loss).
    2. C0/C1 control characters (except LF and HT).
    3. Literal backslash-n sequences in subject or body.
    4. Empty subject line.

.EXAMPLE
  pwsh -NoProfile -File tools/check-commit-message.ps1 -SelfTest

.EXAMPLE
  pwsh -NoProfile -File tools/check-commit-message.ps1 -MessageFile .git/COMMIT_EDITMSG
#>
[CmdletBinding()]
param(
  [switch]$SelfTest,
  [string]$MessageFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-CommitMessageIntegrity {
  param([string]$Text)

  $violations = [System.Collections.Generic.List[string]]::new()

  # Rule 1: U+FFFD replacement character.
  if ($Text.Contains([char]0xFFFD)) {
    $violations.Add('U+FFFD replacement character found (mojibake/encoding loss).')
  }

  # Rule 2: C0/C1 control characters except LF (0x0A) and HT (0x09).
  $bad = [System.Collections.Generic.List[char]]::new()
  foreach ($ch in $Text.ToCharArray()) {
    $code = [int]$ch
    if (($code -ge 0x00 -and $code -le 0x08) -or
        $code -eq 0x0B -or $code -eq 0x0C -or
        ($code -ge 0x0E -and $code -le 0x1F) -or
        ($code -ge 0x7F -and $code -le 0x9F)) {
      $bad.Add($ch)
    }
  }
  if ($bad.Count -gt 0) {
    $samples = @($bad | Select-Object -First 5 | ForEach-Object { 'U+{0:X4}' -f [int]$_ })
    $suffix = ''
    if ($bad.Count -gt 5) { $suffix = " (+$($bad.Count - 5) more)" }
    $violations.Add("C0/C1 control character(s): $($samples -join ', ')$suffix")
  }

  # Rule 3: literal backslash-n sequences.
  if ($Text.Contains('\n')) {
    $violations.Add('Literal backslash-n sequence found.')
  }

  # Rule 4: empty subject.
  $firstLineEnd = $Text.IndexOf([char]0x0A)
  $subjectLine = if ($firstLineEnd -ge 0) { $Text.Substring(0, $firstLineEnd).Trim() } else { $Text.Trim() }
  if (-not $subjectLine) {
    $violations.Add('Subject is empty.')
  }

  # Do not wrap with the unary comma: it would turn an empty result into a
  # one-element array containing an empty array and break caller counts.
  return $violations.ToArray()
}

if ($SelfTest) {
  $caseCount = 0
  $LF = [char]10

  # Case 1: clean multi-line message passes.
  $clean = 'feat: add feature X' + $LF + $LF + 'Body text.' + $LF
  $v = @(Test-CommitMessageIntegrity -Text $clean)
  if ($v.Count -ne 0) { throw ('SelfTest clean flagged: ' + ($v -join '; ')) }
  $caseCount++

  # Case 2: U+FFFD detected.
  $moji = 'fix: broken' + $LF + $LF + 'Has FFFD: ' + [string][char]0xFFFD + [string][char]0xFFFD
  $v = @(Test-CommitMessageIntegrity -Text $moji)
  if (-not ($v | Where-Object { $_ -like '*U+FFFD*' })) { throw 'SelfTest mojibake missed' }
  $caseCount++

  # Case 3: ESC control char detected.
  $esc = 'fix: esc' + $LF + $LF + 'ESC:' + [string][char]0x1B + '[0m]'
  $v = @(Test-CommitMessageIntegrity -Text $esc)
  if (-not ($v | Where-Object { $_ -like '*control*' })) { throw 'SelfTest esc missed' }
  $caseCount++

  # Case 4: BEL control char detected.
  $bel = 'fix: bel' + $LF + $LF + 'BEL:' + [string][char]0x07
  $v = @(Test-CommitMessageIntegrity -Text $bel)
  if (-not ($v | Where-Object { $_ -like '*control*' })) { throw 'SelfTest bel missed' }
  $caseCount++

  # Case 5: literal backslash-n detected in body.
  $litN = 'feat: feature' + $LF + $LF + 'Multi-line via \n escape.'
  $v = @(Test-CommitMessageIntegrity -Text $litN)
  if (-not ($v | Where-Object { $_ -like '*backslash*' })) { throw ('SelfTest literal-backslash-n missed; got: ' + ($v -join '; ')) }
  $caseCount++

  # Case 6: empty subject detected.
  $emptySubj = '' + $LF + $LF + 'Body only.'
  $v = @(Test-CommitMessageIntegrity -Text $emptySubj)
  if (-not ($v | Where-Object { $_ -like '*Subject is empty*' })) { throw 'SelfTest empty-subject missed' }
  $caseCount++

  # Case 7: whitespace-only subject detected.
  $wsSubj = '   ' + $LF + $LF + 'Body.'
  $v = @(Test-CommitMessageIntegrity -Text $wsSubj)
  if (-not ($v | Where-Object { $_ -like '*Subject is empty*' })) { throw 'SelfTest ws-subject missed' }
  $caseCount++

  # Case 8: LF and HT are NOT flagged as control chars.
  $withLfHt = 'subject line' + $LF + 'Tab:' + [char]9 + 'here'
  $v = @(Test-CommitMessageIntegrity -Text $withLfHt)
  if ($v.Count -ne 0) { throw ('SelfTest lf-ht-allowed false positive: ' + ($v -join '; ')) }
  $caseCount++

  # Case 9: C1 control char (0x9B CSI) detected.
  $csi = 'fix: csi' + $LF + $LF + [string][char]0x9B + '0m'
  $v = @(Test-CommitMessageIntegrity -Text $csi)
  if (-not ($v | Where-Object { $_ -like '*control*' })) { throw 'SelfTest c1-csi missed' }
  $caseCount++

  Write-Output "Commit message integrity self-test passed ($caseCount cases)."
  exit 0
}

if (-not $MessageFile) {
  throw 'Usage: -SelfTest or -MessageFile <path> required.'
}

if (-not (Test-Path -LiteralPath $MessageFile)) {
  throw "Message file not found: $MessageFile"
}

$raw = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $MessageFile))
$text = [Text.Encoding]::UTF8.GetString($raw)
$violations = @(Test-CommitMessageIntegrity -Text $text)
if ($violations.Count -gt 0) {
  foreach ($v in $violations) {
    Write-Error "Commit message violation: $v"
  }
  exit 1
}
'Commit message integrity check passed.'

