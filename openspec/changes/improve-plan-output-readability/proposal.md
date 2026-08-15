## Why

The probe plan and post-encode summary are hard to scan: each file spans three lines (`filename + CQ` line, `est. size` line, `ratio` line), every line carries a `[info]` badge, warnings are interleaved with normal entries, and a ratio like `0.80` does not say whether the output grows or shrinks. A 19-file plan prints 57 lines where 20 would do, and the signal that matters (files whose output is larger than the source) is buried.

## What Changes

- The probe plan becomes an aligned table: one row per file, column headers, right-aligned numeric columns, auto-scaling size units (GB/MB), p5 shown with one decimal, and the summary line (per-file `est. size` / `ratio` rows) removed.
- Rows are sorted by file name (UTF-8 byte order); files whose quality floor is unreachable are grouped at the bottom of the table, each prefixed with a warning marker.
- Ratio is expressed as a signed percentage (`−20%`, `+24%`), with an `↑` marker when the estimated output is larger than the source.
- The table adapts to the terminal width: the file-name column takes the remaining width after the numeric columns, is truncated mid-string (extension preserved) when too long, and falls back to a two-line layout on very narrow terminals. No badge prefixes (`[info]`) inside the table body.
- The post-encode summary (`printEncodingSummary`) adopts the same formatting language (structure, alignment, signed percentages for any ratios) without adding new content: counts, failed list, "Needs attention" list, and `Compare:` hints keep their current meaning.
- The unreachable-floor count is surfaced once above the table (e.g. `⚠ 4 files can't reach the floor`), so the anomaly is visible before reading rows.

## Capabilities

### New Capabilities
- `plan-output-formatting`: the terminal output format of the probe plan and the post-encode summary — table layout, sorting, warning grouping, ratio expression, and narrow-terminal adaptation.

### Modified Capabilities

<!-- None: existing requirement text in video-encode-probing/video-preview describes what the plan/summary show, not their layout; the layout contract lives in the new capability. -->

## Impact

- `src/video/encode_probe.cpp` — `printProbePlan` rewritten to build the table (row data already available; needs sort + grouping + width-aware layout).
- `src/video/video_process.cpp` — `printEncodingSummary` restructured to the shared formatting language.
- Possibly a small shared table-layout helper (column width computation, truncation) next to the existing `displaytext` width utilities.
- Tests: unit tests asserting the plan/summary text (existing e2e helpers parse `CQ N` from stdout and must keep matching); no CLI or behavior change beyond printed text.

Affected code: `src/video/encode_probe.cpp`, `src/video/video_process.cpp`, possibly `src/core/display_text.*`; unit tests in `tests/video/encode_probe_tests.cpp` and e2e stdout assertions in `tests/e2e/encro_e2e_tests.cpp` (`extractPlanCq`). No new dependencies; terminal width detection (`consolewidth::resolveColumns`) and Unicode-aware truncation (`displaytext::takePrefixByDisplayWidth`) already exist.
