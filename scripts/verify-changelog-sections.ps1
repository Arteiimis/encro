$ErrorActionPreference = "Stop"
# Run from the repository that holds these scripts, wherever it was cloned.
Set-Location (Split-Path -Parent $PSScriptRoot)

# Every tag has a section; extracting it must produce a body that starts with
# that version's own heading and contains no other version's heading.
# Only `v<version>` tags carry a section; another tag is not a release.
$tags = @(git tag -l | Where-Object { $_ -match '^v\d' } | Sort-Object)
"tags: $($tags.Count)"
$failures = 0
$previousLines = 0

foreach ($t in $tags) {
    $v = $t.Substring(1)
    $body = & pwsh -NoProfile -File scripts/get-changelog-section.ps1 -Version $v 2>&1
    if ($LASTEXITCODE -ne 0) { "  FAIL $v : extractor exited $LASTEXITCODE"; $failures++; continue }
    $first = ($body | Select-Object -First 1)
    $others = @($body | Select-Object -Skip 1 | Where-Object { $_ -match '^## \[' })
    $linkblock = @($body | Where-Object { $_ -match '^\[[^\]]+\]: ' })

    $ok = $true
    if ($first -notmatch "^## \[$([regex]::Escape($v))\]") { "  FAIL $v : first line is '$first'"; $ok = $false }
    if ($others.Count -gt 0) { "  FAIL $v : body contains $($others.Count) other version heading(s): $($others -join ' | ')"; $ok = $false }
    if ($linkblock.Count -gt 0) { "  FAIL $v : body contains $($linkblock.Count) link-definition line(s)"; $ok = $false }
    if (-not $ok) { $failures++ } else { "  ok   $v  ($($body.Count) lines)" }

    if ($v -eq "0.1.0") {
        "       ^ the oldest section, whose terminator is the link block rather than another heading"
    }
}

""
# The unreleased section must not be extractable: no tag names it.
& pwsh -NoProfile -File scripts/get-changelog-section.ps1 -Version "unreleased" 2>&1 | Out-Null
if ($LASTEXITCODE -eq 0) { "  FAIL the unreleased section was extractable"; $failures++ }
else { "  ok   the unreleased section is not extractable (exit $LASTEXITCODE)" }

""
if ($failures -eq 0) {
    "ALL SECTIONS OK ($($tags.Count) versions)"
    exit 0
}
"FAILURES: $failures"
exit 1
