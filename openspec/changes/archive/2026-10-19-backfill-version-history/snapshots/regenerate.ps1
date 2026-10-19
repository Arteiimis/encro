# Regenerates the snapshots under this directory from CHANGELOG.md. Run from
# anywhere; the paths are derived from this file's location.
# Each snapshot is the file as it must read in that tag's tree: the header, the
# [Unreleased] section, that version and every earlier one, and a link block
# cropped to those versions with [unreleased] pointing at the tag itself.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
# -Raw on the working tree would carry the checkout's CRLF; read the blob so
# the output is LF like every other file in the repository.
$lines = (git -C $repoRoot show 'HEAD:CHANGELOG.md') | Out-String -Stream
while ($lines.Count -gt 0 -and [string]::IsNullOrWhiteSpace($lines[-1])) { $lines = $lines[0..($lines.Count - 2)] }
$secStarts = @()
for ($i = 0; $i -lt $lines.Count; $i++) {
  if ($lines[$i] -match '^## \[') { $secStarts += $i }
}
$linkStart = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
  if ($lines[$i] -match '^\[unreleased\]: ') { $linkStart = $i; break }
}

$header = $lines[0..($secStarts[0] - 1)]
$secs = @()
for ($j = 0; $j -lt $secStarts.Count; $j++) {
  $start = $secStarts[$j]
  $end = if ($j -lt $secStarts.Count - 1) { $secStarts[$j + 1] - 1 } else { $linkStart - 1 }
  while ($end -gt $start -and [string]::IsNullOrWhiteSpace($lines[$end])) { $end-- }
  $secs += , @($start, $end)
}
$versions = $secs | ForEach-Object { ($lines[$_[0]] -replace '^## \[([^\]]+)\].*', '$1') }

$linkLines = @()
for ($i = $linkStart; $i -lt $lines.Count; $i++) {
  if ($lines[$i] -match '^\[') { $linkLines += $lines[$i] }
}

# One snapshot per released section, taken from the file itself — a hardcoded
# list here would silently skip a version added to CHANGELOG.md later.
$ascending = @($versions | Where-Object { $_ -ne 'Unreleased' } | Sort-Object {
  [semver]($_)
})

foreach ($v in $ascending) {
  $idx = [array]::IndexOf($versions, $v)
  $out = @()
  $out += $header
  $out += $lines[$secs[0][0]..$secs[0][1]]
  $out += ""
  for ($k = $idx; $k -lt $versions.Count; $k++) {
    $out += $lines[$secs[$k][0]..$secs[$k][1]]
    $out += ""            # keep the blank line between sections that CHANGELOG.md has
  }
  $out += "[unreleased]: https://github.com/Arteiimis/encro/compare/v$v...HEAD"
  for ($k = $idx; $k -lt $versions.Count; $k++) {
    if ($k -eq 0) { continue }
    $target = $versions[$k]
    $match = $linkLines | Where-Object { $_ -match ('^\[' + [regex]::Escape($target) + '\]: ') } | Select-Object -First 1
    if (-not $match) { throw "no link for $target" }
    $out += $match
  }
  ($out -join "`n") + "`n" | Set-Content -Encoding utf8 -NoNewline (Join-Path $PSScriptRoot "CHANGELOG.at-$v.md")
}
