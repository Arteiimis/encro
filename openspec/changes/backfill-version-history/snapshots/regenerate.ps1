# Regenerates snapshots/ from CHANGELOG.md. Run from the repository root.
# Each snapshot is the file as it must read in that tag's tree: the header, the
# [Unreleased] section, that version and every earlier one, and a link block
# cropped to those versions with [unreleased] pointing at the tag itself.
Set-Location (Join-Path $PSScriptRoot "..\..\..\..")

$lines = Get-Content CHANGELOG.md
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

$ascending = @(
  "0.1.0", "0.1.5", "0.2.0", "0.3.0", "0.4.0",
  "1.0.0", "1.1.0", "1.2.0", "1.3.0", "1.4.0", "1.5.0", "1.6.0", "1.7.0",
  "2.0.0-alpha.1", "2.0.0-alpha.2", "2.0.0-alpha.3", "2.0.0-rc.1", "2.0.0"
)

foreach ($v in $ascending) {
  $idx = [array]::IndexOf($versions, $v)
  if ($idx -lt 0) { throw "no CHANGELOG section for $v" }
  $out = @()
  $out += $header
  $out += $lines[$secs[0][0]..$secs[0][1]]
  $out += ""
  for ($k = $idx; $k -lt $versions.Count; $k++) { $out += $lines[$secs[$k][0]..$secs[$k][1]] }
  $out += ""
  $out += "[unreleased]: https://github.com/Arteiimis/encro/compare/v$v...HEAD"
  for ($k = $idx; $k -lt $versions.Count; $k++) {
    if ($k -eq 0) { continue }
    $target = $versions[$k]
    $match = $linkLines | Where-Object { $_ -match ('^\[' + [regex]::Escape($target) + '\]: ') } | Select-Object -First 1
    if (-not $match) { throw "no link for $target" }
    $out += $match
  }
  ($out -join "`n") + "`n" | Set-Content -Encoding utf8 -NoNewline "CHANGELOG.at-$v.md"
}
