## Context

See proposal.md — Why. Current behavior in `src/core/progress.cpp`: `applyBarText` composes `[ETA] | <postfix>` and passes it to `fitPostfixText`, which truncates overflow with `displaytext::truncateWithEllipsis` (progress.cpp:87-163) using a per-part proportional-width allocation. Upstream, the filename label is pre-truncated at 48 columns (`makeSlotLabel` in video_batch_execution.cpp:72-79, `getStateLabel` in video_encoding_state.cpp:31-40) and ffmpeg status lines at 72 chars (`truncateEncodingStatus` in video_encode_runner.cpp:57-60). Postfix budget = console columns − barWidth − 34, minimum 16 (`resolveLayout`, progress.cpp:59-69). The progress bar redraws on every `setPostfixText`/`setProgress` call (worker status callback + 20ms monitor loop), giving a natural tick for scrolling.

## Goals / Non-Goals

**Goals:**
- Overflowing postfix text scrolls as a sliding window; fitting text stays static.
- ETA prefix remains pinned; only postfix content scrolls.
- Full filename/status reach the scroller (remove 48/72 pre-truncation, keep a sanity cap on raw status lines).
- Delete the now-dead per-part proportional allocation logic.

**Non-Goals:**
- Per-bar scroll desynchronization (all bars share one clock; lockstep is acceptable).
- Scroll pauses at text boundaries (wrap is continuous).
- Scrolling for other text surfaces (summary output, logs).
- Terminal width resize detection during a run (layout resolves per redraw, so it adapts naturally anyway).

## Decisions

**D1 — Time-based bounce offset, no per-bar state.** Scroll position = trapezoidal wave of wall-clock time: `bounceOffset(elapsedMs, travel)` sweeps 0→travel (8 cols/sec), pauses ~1s at each end, sweeps back, repeats. Pure function of `steady_clock` — no stored offset, no mutex state, no reset logic when text changes; redraw gaps can't desync position. Alternatives rejected: incremental offset per redraw (pace varies with redraw rate), and the earlier one-way wrap (offset `% (textWidth+budget)`) which scrolled the window through a trailing space-padding region — the user-visible blank gap where no text is shown between wrap cycles. The pause at both ends is a deliberate trapezoid feature: the window dwells on the label's start/end so the name is readable before direction flips.

**D2 — Scroll window is display-width aware, code-point aligned.** `windowByDisplayWidth(text, startCol, maxWidth)` walks UTF-8 code points (reusing `displaytext::utf8CodePointLength` / `displayWidth`), skipping to `startCol`, then taking code points while the accumulated width stays ≤ `maxWidth`. A code point straddling `startCol` is included (rendered from its own start) rather than dropped, preferring readability; the window never exceeds the budget, so no line wrap. `scrollWindow` clamps `startCol` to `textWidth − budget` so the window always ends within the text — no padding region exists (the one-way wrap's blank gap was the padding; bounce replaces it).

**D3 — Replace, not patch, `fitPostfixText`.** New shape: if `displayWidth(text) <= budget` → return text; else → `scrollWindow(text, budget)`. The multi-part `|` proportional allocation is deleted — scrolling makes per-part fairness obsolete since every part becomes visible eventually. `splitPostfixParts` is kept (trim-only), because the fixed-tail split in D4 needs it.

**D4 — Only the label part scrolls; ETA and status tail are pinned.** The postfix `Encoding: <name> | <status>` is split on ` | `. The first part (the label) is the only scroll region; the ETA prefix (`[xxm:yys]`) and the remaining parts (e.g. `segment 1/1`) are fixed text at the left/right, delimiters included, so `[ETA] | <scrolling label> | segment 1/1` never moves anything but the label window. Budget allocation: fixed widths (ETA + delimiters + tail) are subtracted first; the label scrolls in the remainder. If the tail alone would starve the label below a minimal window (12 cols), the tail is truncated with an ellipsis instead of scrolling (scrollBudget ≥ 12 then holds). Alternative (scroll the combined string, D4 old form) rejected: the status tail would scroll off-screen most of each cycle, exactly the readability loss the user reported.

**D5 — Remove upstream pre-truncation, keep a sanity cap.** `makeSlotLabel` / `getStateLabel` stop truncating the filename (return full name). `truncateEncodingStatus`'s 72-char cap is raised to a sanity bound (e.g., 256 chars, applied to raw ffmpeg lines only) so pathological lines can't bloat memory or the scroll string, while realistic names/status pass through whole. `barDone`/idle labels also use the untruncated label — acceptable: they're one-line statuses at completion, and the bar is usually full-width enough; if overly long, `fitPostfixText` will scroll them too, which is consistent.

**D6 — Where scrolling lives: `core/progress.cpp` only.** All callers (`setPostfixText`, initial `makeBar`) flow through `fitPostfixText`, so scrolling applies uniformly to every progress bar (slot bars, overall bar, pack bars) without touching callers. Alternative (scroll only in the video encode callers) rejected: it would duplicate fit logic and leave pack/picture bars truncating.

## Risks / Trade-offs

- [Scrolled window shows a narrow slice on very narrow terminals (16-col budget)] → Acceptable: that's the requirement; full text is reachable by scrolling. ETA pinned per D4.
- [Status updates change text mid-scroll, causing a window jump (time-based offset has no memory)] → Cosmetic; scroll continues from the new text's position. Mitigated by D4 keeping the label prefix `Encoding: <name>` — the filename portion scrolls past at a steady rate.
- [Wide CJK characters near window edges render the window 1-2 cols narrower than budget] → No wrap risk; readability preserved. Accepted in D2.
- [Removing 48-col label truncation lengthens `Done:`/`Failed:` labels after completion] → Rare (only when a name is long); the bar then scrolls, consistent with the new behavior.

## Migration Plan

Pure internal display change; no config, state file, or CLI surface affected. Rollback = revert the commit.

## Open Questions

None — scroll speed and sanity cap are tuning constants with safe defaults (8 cols/s, 256 chars); no spec or task breakdown depends on their exact values.
