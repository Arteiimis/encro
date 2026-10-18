# Prints the changelog section for one released version, for use as a release
# body. The section runs from the version's own `## [<version>]` heading up to
# whichever comes first afterwards:
#
#   * the next `## [` heading, or
#   * the link-definition block (`^[1.2.3]: ...`), which follows the OLDEST
#     section instead of another heading.
#
# The second terminator is not optional: without it the oldest version's body
# would swallow every link line, and with it the names of every other version.
#
# Exits non-zero when the version has no section, so a release cannot be
# published with an empty or invented body.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Path = (Join-Path (Split-Path -Parent $PSScriptRoot) "CHANGELOG.md")
)

$ErrorActionPreference = "Stop"

$normalized = $Version.TrimStart("v", "V")
$lines = [System.IO.File]::ReadAllLines($Path)

$heading = "^## \[$([regex]::Escape($normalized))\]"
$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -cmatch $heading) { $start = $i; break }
}
if ($start -lt 0) {
    [Console]::Error.WriteLine("no changelog section for version '$normalized' in $Path")
    exit 1
}

$end = $lines.Count
for ($i = $start + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^## \[' -or $lines[$i] -match '^\[[^\]]+\]: ') { $end = $i; break }
}

# Trim trailing blank lines so the body does not end with a gap.
$last = $end - 1
while ($last -gt $start -and [string]::IsNullOrWhiteSpace($lines[$last])) { $last-- }

for ($i = $start; $i -le $last; $i++) { $lines[$i] }
