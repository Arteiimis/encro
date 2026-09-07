## Context

The narration lines and summary blocks live in the pipeline entry points: `src/video/video_process.cpp` (scan lines, summary), `src/video/video_batch_execution.cpp` (scheduling line), `src/video/encode_probe.cpp` (probing-complete line, plan rendering), `src/picture/picture_process.cpp` (announcements), `src/pack/packer.cpp` (scan wording), `src/organize/report.cpp` (rule glyph). The plan table and its narrow-terminal fallback already live in `encode_probe.cpp` with display-width helpers in `src/core/display_text.h`. See proposal.md - Why for the line-by-line evidence.

## Goals / Non-Goals

**Goals:**

- One narration line per phase outcome on non-TTY output; TTY keeps live start lines.
- Summary says the one thing that happened; failure sections appear only when they have content.
- No misleading lines ("probing complete" after skipped probing, "all completed" before a failure count).

**Non-Goals:**

- No progress-bar changes (bars, tone colors, erase behavior — owned by the `progress-*` specs).
- No per-file failure reason text — owned by the `failure-reason-visibility` change; this change only reshapes the summary so reasons slot into the failed-file list later.
- No help-text or flag changes.

## Decisions

- **D1 — TTY gate reuses the existing terminal probe.** The same `isatty`/color-mode check that gates progress bars gates scan start lines; no new detection. In tests and pipes the completion line is the only scan line, matching what the e2e suites already capture.
- **D2 — collapse predicate is "no row carries measured data".** The plan renderer checks whether any pending file has a probe result; if none does, it prints `{n} video(s) to encode at CQ {cq} (probing skipped: {reason})` and skips rules, headers, and totals. Mixed batches keep the table; unprobed rows get a skip-note suffix reusing the existing `(cached)` / `(skipped: ...)` suffix machinery. Alternative rejected: always printing the table with dashes — that is the current misleading output this change removes.
- **D3 — summary shape.** Success: `Encoded {n}/{n} videos → {output-dir}` — the resolved output directory always appears, including when it is the implicit default next to the inputs (resolved once by the existing output-path logic), followed by the existing one-line preview hint. Failure: the same count line with the failing count, then the failed list, then attention/preview-hint lines when applicable. The count line is assembled at the same site that today prints the five-line block (`video_process.cpp` summary builder), so the failure-reason change has one insertion point.
- **D4 — counts in user terms.** Scan completion lines name videos/pictures/files per mode instead of `candidate file(s)`; `(recursive=true)` is dropped (recursivity is default behavior, shown in help).
- **D5 — ellipses and rule glyphs.** Narration trailing ellipses are pinned to ASCII `...` (they already are everywhere; the pinning test prevents drift); the filename truncation marker stays `…` because it marks mid-string truncation and must not be confused with a sentence trailing off. The organize report's `-----` rules switch to `─` to match the plan; both reports render through the same terminal helpers, so no charset fallback is added (the plan has shipped `─` unconditionally already).
- **D6 — removed scheduling line stays in the log.** The batch-start log record (preparing batch, counts) already exists at info level, so echo keeps concurrency visible; nothing is lost, only the console narration.

## Risks / Trade-offs

- [Tests assert removed lines] → Mechanical sweep of narration assertions in unit/e2e suites, same commit; `xmake test-parallel` gates.
- [Users who watched the scheduling line lose a cue] → The overall progress bar shows concurrency implicitly (per-slot bars in `-F`); `-v` echo keeps the explicit record.
- [One-line summary hides counts users want] → The line carries the count and destination; size totals remain in the plan table and log. If feedback wants them in the summary, append to the same line later without a format break.

## Migration Plan

Single build, no persisted state. Console output format changes only; nothing reads it back. Rollback is reverting the commit.
