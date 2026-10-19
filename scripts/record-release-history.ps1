# Records a release entry for every tag at or below a boundary version that has
# none, with the never-distributed paragraph and no artifacts.
#
# The boundary is what makes this safe to re-run: tags ABOVE it are never
# touched, so a version whose publish attempt failed stays visible as a missing
# entry instead of being recorded as "never distributed".
#
# Ordering uses System.Management.Automation.SemanticVersion rather than sort -V,
# because version-sort puts 2.0.0 before 2.0.0-alpha.1 while semver puts every
# pre-release BELOW its final release — which would silently skip the pre-release
# tags this history contains.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Until,
    [switch]$WhatIfOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $boundary = [semver]($Until.TrimStart("v", "V"))
    $created = 0
    $kept = 0
    $considered = 0

    $tags = @(git tag -l | Where-Object { $_ -match '^v' })
    foreach ($tag in $tags) {
        $version = $tag.Substring(1)
        $parsed = $null
        if (-not [semver]::TryParse($version, [ref]$parsed)) {
            Write-Host "  skip $tag (not a version)"
            continue
        }
        if ($parsed -gt $boundary) { continue }
        $considered++

        $existing = $null
        try { $existing = gh release view $tag --json tagName 2>$null } catch { $existing = $null }
        if ($existing) {
            $kept++
            Write-Host "  keep  $tag (already has an entry)"
            continue
        }

        Write-Host "  create $tag"
        if (-not $WhatIfOnly) {
            $section = & pwsh -NoProfile -File (Join-Path $PSScriptRoot "get-changelog-section.ps1") -Version $version
            if ($LASTEXITCODE -ne 0) { throw "no changelog section for $version" }
            $body = @(
                "> **This version was never distributed.** encro began publishing"
                "> downloadable binaries at a later version. This entry records the"
                "> project's history and carries nothing to install."
                ""
            ) + $section
            $tmp = [System.IO.Path]::GetTempFileName()
            [System.IO.File]::WriteAllLines($tmp, $body, [System.Text.UTF8Encoding]::new($false))
            gh release create $tag --title $tag --notes-file $tmp
            if ($LASTEXITCODE -ne 0) { throw "gh release create failed for $tag" }
            Remove-Item $tmp -Force
        }
        $created++
    }

    # Anything above the boundary must still have no entry, or the boundary
    # logic silently regressed. Checking it rather than counting it is the
    # difference between a guard and a log line.
    $above = @($tags | Where-Object {
        $v = $_.Substring(1); $p = $null
        [semver]::TryParse($v, [ref]$p) -and $p -gt $boundary
    })
    foreach ($t in $above) {
        gh release view $t *> $null
        if ($LASTEXITCODE -eq 0) {
            throw "$t is above the boundary ($Until) but already has a release; the boundary logic regressed"
        }
    }
    Write-Host ""
    Write-Host "boundary $Until : considered $considered tags, created $created, already present $kept"
    Write-Host "tags above the boundary left alone: $($above.Count) ($($above -join ', '))"
}
finally {
    Pop-Location
}
