## Why

On narrow terminals, the `Encoding: <filename> | <status>` text on each progress bar is truncated with an ellipsis (`...`) the moment it exceeds the postfix budget, so long video names lose almost all readable information while encoding runs.

## What Changes

- Postfix text on progress bars that exceeds the available postfix width SHALL scroll (marquee-style sliding window) instead of being truncated with an ellipsis, so the full filename and status become visible over time.
- Text that fits within the postfix budget SHALL render statically as today (no scroll).
- The ETA prefix on bars that have one SHALL stay visible while the rest of the postfix text scrolls.
- Upstream pre-truncation of the encode label and status text (48-char filename label, 72-char status lines) SHALL be removed so scrolling operates on the full text; a sanity cap remains to bound pathological ffmpeg status lines.
- Dead code from the old fit logic (per-part proportional width allocation) SHALL be removed.

## Capabilities

### New Capabilities
- `progress-scroll-label`: Scrolling (marquee) display of progress-bar postfix text when it overflows the terminal postfix budget.

### Modified Capabilities
<!-- none -->

## Impact

- `src/core/progress.cpp` / `src/core/progress.h` — `fitPostfixText` overflow behavior, `applyBarText` ETA handling, new scroll-window helper (UTF-8 / display-width aware, time-based offset).
- `src/video/video_batch_execution.cpp` — `makeSlotLabel` / `truncateForProgressLabel` pre-truncation removed.
- `src/video/video_encoding_state.cpp` — `getStateLabel` pre-truncation removed.
- `src/video/video_encode_runner.cpp` — `truncateEncodingStatus` cap raised/replaced by a sanity bound.
- No external dependencies; `indicators` and `displaytext` helpers already in repo.
