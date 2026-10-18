# Verifies the release tag line against its own contract: the semver grammar
# from the change's version-numbering spec, the ancestry order, and the
# CHANGELOG shape from the changelog-maintenance spec.
#
#   pwsh scripts/verify-release-tags.ps1
#
# Reads the tags from git, so it needs no fixture and runs after any release.
# Exits 1 and lists every failure when a check does not hold.
$ErrorActionPreference = 'Stop'
$failures = @()
function Fail([string]$message) { $script:failures += $message }

$tags = @(git tag -l | Sort-Object)
if ($tags.Count -eq 0) { Write-Output 'no tags found'; exit 1 }

# --- 1. tag names conform, and the grammar really rejects bad ones ---------
$id = '(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'
$grammar = "^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-$id(\.$id)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$"
foreach ($t in $tags) {
  # -cmatch: -match is case-insensitive and would accept 'V1.7.0'.
  if ($t -cnotmatch $grammar) { Fail "tag name does not conform to semver: $t" }
  if ((git cat-file -t $t) -ne 'tag') { Fail "tag is not annotated: $t" }
}
foreach ($bad in @('v1.7', 'V1.7.0', 'release-1.7.0', 'v2.0.0-alpha.01', 'v01.0.0', 'v2.0.0-', 'v1.7.0.1')) {
  if ($bad -cmatch $grammar) { Fail "the tag grammar wrongly accepts '$bad'" }
}
foreach ($good in @('v2.0.0', 'v2.0.0-alpha.1', 'v1.7.0', 'v2.0.0+build.1', 'v0.1.0')) {
  if ($good -cnotmatch $grammar) { Fail "the tag grammar wrongly rejects '$good'" }
}

# --- 2. precedence order equals commit order -------------------------------
# [semver] is PowerShell's own semver 2.0.0 implementation, so precedence is
# not re-derived here. -Stable keeps equal-precedence tags (build metadata
# only) in git's order instead of comparing them equal.
$bySemver = @($tags | Where-Object { $_ -cmatch $grammar } | Sort-Object -Stable { [semver]($_.Substring(1)) })

$revs = @(git rev-list main --first-parent)
$pos = @{}
for ($i = 0; $i -lt $revs.Count; $i++) { $pos[$revs[$i]] = $i }
$byHistory = @($tags | Sort-Object {
  $c = (git rev-parse "$_^{commit}").Trim()
  if ($pos.ContainsKey($c)) { $pos[$c] } else { [int]::MaxValue }
} -Descending)

if (($byHistory -join ',') -ne ($bySemver -join ',')) {
  Fail "precedence order does not match commit order - history: [$($byHistory -join ', ')] - semver: [$($bySemver -join ', ')]"
}
for ($i = 1; $i -lt $bySemver.Count; $i++) {
  if ([semver]$bySemver[$i - 1].Substring(1) -ge [semver]$bySemver[$i].Substring(1)) {
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

# --- 4. CHANGELOG covers every tag, newest first, with working links ------
$lines = Get-Content CHANGELOG.md
$released = @()
$prevDate = $null
foreach ($l in $lines) {
  if ($l -match '^## \[(.+?)\](?: - (\d{4}-\d{2}-\d{2}))?') {
    $v = $Matches[1]
    if ($v -eq 'Unreleased') { continue }
    $released += $v
    if (-not $Matches[2]) { Fail "CHANGELOG section has no ISO date: $v"; continue }
    $d = [datetime]::ParseExact($Matches[2], 'yyyy-MM-dd', $null)
    if ($prevDate -and $d -ge $prevDate) { Fail "CHANGELOG dates do not descend at $v" }
    $prevDate = $d
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

# --- 5. change types are legal and no group is empty ---------------------
$allowed = @('Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security')
$currentType = $null
$currentTypeHadEntry = $false
foreach ($l in $lines) {
  if ($l -match '^### (.+)$') {
    if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType'" }
    $type = $Matches[1].Trim()
    if ($allowed -notcontains $type) { Fail "unknown change type '$type' (allowed: $($allowed -join ', '))" }
    $currentType = $type
    $currentTypeHadEntry = $false
    continue
  }
  if ($l -match '^## \[(.+?)\]') {
    $section = $Matches[1]
    if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType' in section $section" }
    $currentType = $null
    $currentTypeHadEntry = $false
    continue
  }
  if ($currentType -and $l -match '^- ') { $currentTypeHadEntry = $true }
}
if ($currentType -and -not $currentTypeHadEntry) { Fail "empty group '### $currentType' at end of file" }

# --- report ---------------------------------------------------------------
if ($failures.Count -eq 0) {
  Write-Output "OK: $($tags.Count) tags verified (name grammar, annotated, precedence vs commit order, per-tag CHANGELOG section, $(($released | Measure-Object).Count) CHANGELOG sections, $($links.Count) links, legal change types)"
  exit 0
}
Write-Output "FAILURES ($($failures.Count)):"
$failures | ForEach-Object { Write-Output "  - $_" }
exit 1
