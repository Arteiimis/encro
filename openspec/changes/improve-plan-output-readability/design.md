# Design: Plan Output Readability

## Context

- `printProbePlan` (`src/video/encode_probe.cpp`) prints one `[info]` line per file (name + CQ + p5), then two indented continuation lines (`est. size:` and `ratio:`); the `Total:` line closes. Input order is preserved.
- `printEncodingSummary` (`src/video/video_process.cpp`) prints counts, a failed list, the "Needs attention" list, and `Compare:` hints.
- Available infrastructure: `consolewidth::resolveColumns()` (terminal width, COLUMNS env override, Windows/POSIX), `displaytext::displayWidth` / `takePrefixByDisplayWidth` (Unicode-aware width and truncation, CJK-safe), and `terminal::renderMessage` which adds a badge prefix for message kinds except `Plain`/`Heading`.
- See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**
- One aligned row per file in the plan; warning rows grouped and marked; signed-percentage ratios with growth indicator.
- Same formatting language in the post-encode summary without changing its content (option A).
- Narrow-terminal handling that never loses numeric data.

**Non-Goals:**
- Adding a success-file table to the post-encode summary (option B; future change).
- Machine-readable output (JSON/CSV), color policy changes, or changing what data the plan contains.

## Decisions

### 1. Table body prints through `MessageKind::Plain`, one multi-line `write`

`terminal::println(Info, ...)` prepends the `[info]` badge to every line, which would break column alignment. The table body (header + rows) is assembled as one string and emitted with `terminal::write(Stream::Stdout, text, true)` — plain text, no badge, alignment intact. The table title keeps `Info` (single badge line), and warning rows use the `⚠` text marker plus (when colors are enabled) `terminal::styledText` for the marker — never a per-row `Warning` kind, which would re-introduce the badge.

*Alternative considered*: per-row `println(Warning, ...)` for warning rows — rejected: badge prefixes misalign the columns.

### 2. One width computation, two layouts

```
numeric width ≈ CQ(3) + p5(6) + size(9) + ratio(6) + 3 gaps of 2 ≈ 32 columns
nameWidth = terminalWidth − 2 (indent) − 32
```
- `nameWidth ≥ 20` (terminal ≥ 52 cols): table layout; names longer than the column are truncated mid-string with `…` keeping the extension (`35e5a22dece…e21.mp4`).
- `nameWidth < 20` (terminal < 52 cols, pathological): two-line fallback per spec (name line, indented metrics line). No data loss.

The width computation is a pure function `layoutColumns(terminalWidth) -> optional<TableLayout>`; `TableLayout` carries the name-column width, so unit tests can assert exact strings at fixed widths without a real terminal. Non-TTY output (e2e, unit tests, CI) resolves columns to a fixed default (existing `consolewidth` behavior, 80) and simply uses that width.

*Alternatives considered*: a single truncating name column at all widths — rejected: below ~50 columns the name column would show fewer than 20 usable characters, which is less useful than the two-line fallback. A middle "numeric-shrink" band (fixed MB units, tighter gaps) was designed but dropped during implementation: the fixed numeric width is 32 columns regardless of unit choice, so the band saved at most one or two columns — not worth a second layout.

### 2b. p5 column precision

The p5 column shows one decimal at or above the default floor (`95.0`), two decimals below it (`3.72`, `91.56`) so abnormal scores stay visibly precise; SSIM values use three decimals (`0.980`). Sorted rows never compare a VMAF value against an SSIM value directly (rows are grouped by status, not by metric).

### 3. Sorting and grouping by file name (UTF-8 byte order)

`printProbePlan` partitions plans into `normal` and `unreachable`, sorts each by `inputPath.filename()` byte order (stable, locale-independent), prints the unreachable count line, then both groups. The `Total:` line keeps its existing contents and gains the signed-percentage ratio. A single-file plan is unaffected (one row, no grouping).

*Alternative considered*: locale-aware collation — rejected: byte order is deterministic across machines and matches what the rest of the tool does; CJK file names sort consistently under UTF-8 byte order.

### 4. Signed-percentage ratio with growth marker

`ratioText(ratio)` renders `−20%` (U+2212 minus for visual alignment with CJK-friendly terminals, or ASCII `-` when colors/Unicode are disabled) and appends `↑` when ratio > 1.0. The `Total:` line reuses the same helper. The marker is a text character, not a color, so it survives non-TTY output and tests.

### 5. Post-encode summary: restructure only

`printEncodingSummary` keeps every current line's meaning; the change is limited to layout conventions (counts aligned, lists consistent, any future ratio via the shared helper). The shared helper(s) live in `src/core/display_text.{h,cpp}` next to the existing width utilities: `formatSignedPercent(ratio)`, `truncateMiddle(name, width)`, and `layoutColumns(width)`.

*Alternative considered*: moving summary rendering into a new module — rejected: the summary is a leaf function; sharing two small helpers is enough.

## Risks / Trade-offs

- **Existing e2e text assertions** (`extractPlanCq` scanning stdout for `CQ N`, tests asserting plan lines) will need updating to the new row format — mechanical, covered by the same tests.
- **CJK file names in the name column**: width measured with `displaytext::displayWidth` (double-width cells); truncation is safe but a CJK name cut mid-grapheme is avoided by truncating at codepoint boundaries (existing helper behavior).
- **U+2212 minus in terminals without glyph support**: falls back to ASCII `-` when the terminal reports no Unicode/color support (same switch the badge rendering uses).
- **The 50–69 column numeric-shrink band adds a small amount of code**: accepted as the only middle step between full table and two-line fallback; unit tests pin its behavior.
