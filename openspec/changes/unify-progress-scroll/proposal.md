## Why

Postfix scrolling only animates on the encode path. The scroll offset is a pure function of wall-clock time (`bounceOffset`), but a bar only repaints when a setter runs, so the encode monitor drives `ProgressContext::tick()` every 100 ms while nothing else does. Probe, preview, and pack bars therefore freeze for the entire duration of a probe step, a window score, or a long single-file pack step and then jump to a new offset, while encode bars keep scrolling smoothly (pack's two spinner threads repaint only while a finalize is in flight). Probe and preview additionally join their postfix with `·`, a separator the scroller does not recognize as a label/status boundary, so their status (`CQ 20 scoring`, sub-step) scrolls off-screen instead of staying pinned the way encode's `Encoding: <file> | <status>` does. Both are display inconsistencies in the same surface, and the second one contradicts the spec's "status stays fixed" scenario.

## What Changes

- Give `progress::ProgressContext` its own repaint clock: the first added bar starts a repaint thread (≈100 ms) that only repaints, never touches progress or ETA sampling; `eraseBars()` and destruction stop it. Every mode — encode, probe, preview, pack, picture — then animates identically with no per-command timer.
- Remove the encode-specific timer: delete `kScrollTickInterval` and the `progress().tick()` block from `monitorEncodingProgress` in `src/video/video_encoding_state.cpp`.
- Unify the postfix grammar on ` | `: convert the 4 probe/preview postfixes that use `·` (`src/video/encode_probe.cpp`, `src/preview/preview_process.cpp`) so the label scrolls and the status stays pinned, matching encode and the existing "only the label part scrolls" scenario.
- Expose read-only views on `ProgressContext` — a repaint counter and the bar's current remaining-time estimate, alongside the existing `elapsedSeconds` accessor (same spirit: diagnostics and tests) — so a `[progress]` unit test can assert the repaint clock runs and that repaints leave progress and estimate alone, polled with `testutils::waitUntil`.
- Not changing: non-TTY output stays silent (no bars, no frames); bar width/budget layout, scroll speed (8 cols/s), pause duration, and ETA math; the pack finalize spinner frames; the picture counters (`Compressing: 3/120`) get no status tail; setter calls keep rendering immediately rather than coalescing into the timer.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `progress-scroll-label`: adds the requirement that every progress postfix — not just encode's — uses the ` | ` label/status grammar (first part scrolls, later parts stay pinned), and the requirement that the scroll animation repaints on its own timer for every bar and every mode, independent of progress-data updates.

## Impact

- `src/core/progress.{h,cpp}`: repaint thread ownership, lifetime rules (start on first bar, stop in `eraseBars()`/destructor), repaint counter and estimate accessors.
- `src/video/video_encoding_state.cpp`: remove the bespoke scroll tick and its interval constant.
- `src/video/encode_probe.cpp` (2 postfixes), `src/preview/preview_process.cpp` (2 postfixes): `·` → ` | `.
- Read-only beneficiaries, no code change: `src/pack/*` (pack bars; its two 120 ms spinner threads in `packer.cpp` and `pack_service.cpp` keep rotating their frame character), `src/picture/*`, `src/video/video_batch_execution.h` (encode bars).
- Tests: `tests/infra/progress_tests.cpp` gains the repaint-clock test; existing `tick()`/`fitPostfixWithEta`/`bounceOffset` tests stay valid. No CLI, config, state-file, or log-format surface changes.
