## 1. Tests first (RED)

- [x] 1.1 Add unit tests in `tests/display_text_tests.cpp` for a new display-width window helper (`takeWindowByDisplayWidth`): window at offset 0 equals the prefix; mid-text offsets return the correct slice; window width never exceeds `maxWidth`; UTF-8/CJK text never yields a window that splits a code point.
- [x] 1.2 Add unit tests in `tests/infra/progress_tests.cpp` for the scroll fit function: text that fits is returned verbatim (no `...`); overflowing text is returned without ellipsis, at most `budget` display columns wide, and advances when the offset changes; wide CJK text scrolls without corruption.
- [x] 1.3 Add unit tests for the ETA-composition path: with an ETA prefix, the ETA text is always present in the output while the postfix part scrolls; without an ETA, output is the plain fit result.
- [x] 1.4 Verify the new tests fail (compile error or assertion) before implementation — `xmake build tests && xmake run tests "[progress]"`.

## 2. Scroll display in progress context

- [x] 2.1 Add `takeWindowByDisplayWidth(text, startCol, maxWidth)` to `src/core/display_text.h` (code-point aligned, mirrors existing `takePrefixByDisplayWidth`).
- [x] 2.2 Expose the fit/scroll functions in `src/core/progress.h` (`fitPostfixText`, plus an ETA-aware variant) so unit tests can reach them.
- [x] 2.3 Rewrite `fitPostfixText` in `src/core/progress.cpp`: text that fits is returned as-is; overflow returns a scrolling window (right-padded with spaces, time-based offset via `steady_clock`, ~8 cols/sec, wraps around). Delete the per-part proportional allocation and `splitPostfixParts` dead code.
- [x] 2.4 Update `applyBarText` to pin the ETA prefix (`[..]`) while the postfix content scrolls in the remaining width (design D4).

## 3. Remove upstream pre-truncation

- [x] 3.1 `src/video/video_batch_execution.cpp`: `makeSlotLabel` / `truncateForProgressLabel` stop truncating the filename to 48 columns.
- [x] 3.2 `src/video/video_encoding_state.cpp`: `getStateLabel` stops truncating the filename to 48 columns.
- [x] 3.3 `src/video/video_encode_runner.cpp`: raise `truncateEncodingStatus` cap from 72 to a sanity bound (256 chars) applied to raw ffmpeg lines.

## 4. Verify

- [x] 4.1 `xmake format` then `xmake build tests && xmake run tests` — full suite green.
- [x] 4.2 Manual narrow-terminal check (e.g., `mode con: cols=80`): encode a file with a long name and confirm the `Encoding:` text scrolls, ETA stays visible, and no line wrapping occurs.
