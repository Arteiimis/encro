# Verifies the release tag line against the rules in docs/public-api.md and
# CHANGELOG.md's own contract. Reads the tags from git, so it needs no fixture
# and can run after any release.
#
#   pwsh scripts/verify-release-tags.ps1
#
# Exits 1 with a list of failures when any check does not hold.
$ErrorActionPreference = 'Stop'
$tick = [char]96

function Fail([string]$message) { $script:failures += $message }

$failures = @()

$tags = @(git tag -l | Sort-Object)
if ($tags.Count -eq 0) { Write-Output 'no tags found'; exit 1 }

# --- 1. every tag name is a conforming semver tag -------------------------
$id = '(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
$grammar = "^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-$id(\.$id)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$"
foreach ($t in $tags) {
  if ($t -notmatch $grammar) { Fail "tag name does not conform to semver: $t" }
  if ((git cat-file -t $t) -ne 'tag') { Fail "tag is not annotated: $t" }
}

# --- 2. the commit order of the tags agrees with their precedence --------
function Get-Key([string]$tag) {
  $core, $pre = ($tag.Substring(1) -split '-', 2)
  $n = $core -split '\.' | ForEach-Object { [int]$_ }
  [pscustomobject]@{ Major = $n[0]; Minor = $n[1]; Patch = $n[2]; Pre = $pre }
}

function Compare-Pre($a, $b) {
  if ($null -eq $a -and $null -eq $b) { return 0 }
  if ($null -eq $a) { return 1 }        # a normal version outranks a pre-release
  if ($null -eq $b) { return -1 }
  $as = $a.Split('.'); $bs = $b.Split('.')
  for ($i = 0; $i -lt [Math]::Min($as.Count, $bs.Count); $i++) {
    $xn = 0; $yn = 0
    $xNum = [int]::TryParse($as[$i], [ref]$xn)
    $yNum = [int]::TryParse($bs[$i], [ref]$yn)
    if ($xNum -and $yNum) { if ($xn -ne $yn) { return $xn - $yn } }
    elseif ($xNum) { return -1 }        # numeric identifiers sort below alphanumeric
    elseif ($yNum) { return 1 }
    else { $c = [string]::CompareOrdinal($as[$i], $bs[$i]); if ($c -ne 0) { return $c } }
  }
  return $as.Count - $bs.Count
}

function Compare-Tag($x, $y) {
  $a = Get-Key $x; $b = Get-Key $y
  $c = $a.Major - $b.Major
  if ($c -eq 0) { $c = $a.Minor - $b.Minor }
  if ($c -eq 0) { $c = $a.Patch - $b.Patch }
  if ($c -eq 0) { $c = Compare-Pre $a.Pre $b.Pre }
  return $c
}

# Order the tags two ways and require the same sequence: by position in the
# first-parent history (oldest first) and by semver precedence.
$revs = @(git rev-list main --first-parent)
$pos = @{}
for ($i = 0; $i -lt $revs.Count; $i++) { $pos[$revs[$i]] = $i }
$byHistory = @($tags | Sort-Object {
  $c = (git rev-parse "$_^{commit}").Trim()
  if ($pos.ContainsKey($c)) { $pos[$c] } else { [int]::MaxValue }
} -Descending)

$bySemver = @($tags)
for ($i = 0; $i -lt $bySemver.Count; $i++) {
  for ($j = $i + 1; $j -lt $bySemver.Count; $j++) {
    if ((Compare-Tag $bySemver[$i] $bySemver[$j]) -gt 0) {
      $tmp = $bySemver[$i]; $bySemver[$i] = $bySemver[$j]; $bySemver[$j] = $tmp
    }
  }
}

if (($byHistory -join ',') -ne ($bySemver -join ',')) {
  Fail "precedence order does not match commit order - history: [$($byHistory -join ', ')] - semver: [$($bySemver -join ', ')]"
}
for ($i = 1; $i -lt $bySemver.Count; $i++) {
  if ((Compare-Tag $bySemver[$i - 1] $bySemver[$i]) -ge 0) {
    Fail "two tags share a version: $($bySemver[$i - 1]) and $($bySemver[$i])"
  }
}

# --- 3. every tag's tree carries its own newest CHANGELOG section ---------
$headingPattern = '^## \[(.+?)\]'
foreach ($t in $tags) {
  $version = $t.Substring(1)
  $heads = git show "${t}:CHANGELOG.md" 2>$null | Select-String -Pattern $headingPattern | ForEach-Object { $_.Matches[0].Groups[1].Value }
  if (-not $heads) { Fail "no CHANGELOG.md in the tree of $t"; continue }
  if ($heads[0] -ne 'Unreleased') { Fail "$t : first CHANGELOG heading is '$($heads[0])', expected Unreleased" }
  if ($heads.Count -lt 2) { Fail "$t : CHANGELOG has no released section"; continue }
  if ($heads[1] -ne $version) { Fail "$t : newest released section is '$($heads[1])', expected '$version'" }
}

# --- 5. CHANGELOG covers every tag, newest first, with links --------------
$lines = Get-Content CHANGELOG.md
$released = @()
$prevDate = $null
foreach ($l in $lines) {
  if ($l -match $headingPattern) {
    $v = $Matches[1]
    if ($v -eq 'Unreleased') { continue }
    $released += $v
    if ($l -match '- (\d{4}-\d{2}-\d{2})') {
      $d = [datetime]$Matches[1]
      if ($prevDate -and $d -ge $prevDate) { Fail "CHANGELOG dates do not descend at $v" }
      $prevDate = $d
    } else { Fail "CHANGELOG section has no ISO date: $v" }
  }
}

$tagVersions = $tags | ForEach-Object { $_.Substring(1) }
foreach ($v in $tagVersions) { if ($released -notcontains $v) { Fail "tag exists without a CHANGELOG section: $v" } }
foreach ($v in $released) { if ($tagVersions -notcontains $v) { Fail "CHANGELOG section exists without a tag: $v" } }

$links = @()
foreach ($l in $lines) { if ($l -match '^\[(.+?)\]: (\S+)$') { $links += [pscustomobject]@{ Name = $Matches[1]; Url = $Matches[2] } } }
if (-not ($links | Where-Object { $_.Name -eq 'unreleased' })) { Fail 'link block has no [unreleased] entry' }
foreach ($v in $released) {
  $link = $links | Where-Object { $_.Name -eq $v } | Select-Object -First 1
  if (-not $link) { Fail "no link for $v"; continue }
  if ($v -eq $released[-1]) {
    if ($link.Url -notmatch '/releases/tag/v') { Fail "the lowest version should link to its release page, not a compare: $v" }
  } elseif ($link.Url -notmatch '/compare/v.+\.\.\.v') { Fail "link is not a compare URL: $v" }
}

# --- 6. change types are legal and no group is empty -------------------
$allowed = @('Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security')
$currentType = $null
$currentTypeHadEntry = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
  $l = $lines[$i]
  if ($l -match '^#### ') { continue }                # scenarios never appear in CHANGELOG, be lenient anyway
  if ($l -match '^### (.+)$') {
    if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType'" }
    $type = $Matches[1].Trim()
    if ($allowed -notcontains $type) { Fail "unknown change type '$type' (allowed: $($allowed -join ', '))" }
    $currentType = $type
    $currentTypeHadEntry = $false
    continue
  }
  if ($l -match '^## \[') {
    if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType' in section $($Matches[0])" }
    $currentType = $null
    $currentTypeHadEntry = $false
    continue
  }
  if ($currentType -and $l -match '^- ') { $currentTypeHadEntry = $true }
}
if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType' at end of file" }

# --- report ---------------------------------------------------------------
if ($failures.Count -eq 0) {
  Write-Output "OK: $($tags.Count) tags verified (grammar, annotated, precedence, ancestry, per-tag CHANGELOG, $($released.Count) CHANGELOG sections, $($links.Count) links)"
  exit 0
}
Write-Output "FAILURES ($($failures.Count)):"
$failures | ForEach-Object { Write-Output "  - $_" }
exit 1
