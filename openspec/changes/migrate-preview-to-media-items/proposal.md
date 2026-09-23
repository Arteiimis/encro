## Why

Preview is the last flow that still carries its own parallel-window machinery. Its single-input mode hand-builds one `taskexec::TaskSpec` per window, counts completions with its own atomic — `windowsCompleted` (`src/preview/preview_process.cpp:425`) is incremented only after a successful outcome write (`:451-452`) — and copies each result out through a per-index `WindowOutcome` vector. Its two-input mode scores windows in a plain sequential loop (`:713-771`) that drives the same bar with its own `done = index + 1` counter (`:750-760`). Neither site uses the item contract or `runStage`, so preview duplicates what `unify-media-item-and-stages` removed from picture and organize and `migrate-video-to-media-items` removed from video: the `TaskSpec` construction (`:431-467`), the completion counter, the outcome write-back and the per-completion bar update (`:453-463`).

Why now: the abstraction was built with preview's shape in mind. `unify-media-item-and-stages` D8 named preview's "draw no bar of my own" mode (`progress = nullptr` while painting through a captured `BarSlot`) as a case `runStage` must support, and `migrate-video-to-media-items` D5 recorded preview as the named next user. With preview migrated, the only flow left off the runner is pack, which is out by design.

## What Changes

- **One file-private item type for both modes.** `PreviewWindowItem` = `{index, Window, segmentPath, sourcePath, ItemOutcome}` with `id()`/`label()`/`source()`/`outcome()`, declared in `src/preview/preview_process.cpp`'s anonymous namespace (no new header). Single-input items carry `segmentPath = <probeRoot>/win<i>.ts`; two-input items leave it empty. `id`/`label` keep today's `"preview-window:{i}"` / `"window {i}"`, and `source()` returns the original path so the task records keep today's `input` attribute (`task_executor.cpp:36-43`).
- **Both bar sites run on `runStage`.** The single-input window batch (`encodeAndScoreAllWindows`, `:416-475`) keeps its concurrency (`clamp(maxParallelJobs, 1, windows.size())`) and loses its `TaskSpec` loop, its `windowsCompleted` counter and its bar update; the two-input scoring loop becomes the same stage with `maxConcurrency = 1`. Both pass `setBarProgress = false` and a `postfix` that owns the bar's value and text, the shape video already uses (`video_batch_execution.cpp:570-583`).
- **`WindowOutcome`, `WindowBatchResult` and `windowsCompleted` are deleted.** The `std::vector<Window>` that `FiltergraphSpec` (`preview_filtergraph.h:29-37`) and `printWindows` (`:203-227`) need is rebuilt from the items after the stage — the runner preserves input order — and `findWorstWindow` reads the items instead of an outcomes vector.
- **`StageSpec` regains an optional `hideCursor`.** `runStage` hardcodes `hideCursor = spec.progress != nullptr` (`media_item.h:157`), which is wrong for preview: its bar spans probe, windows and render, and the flow owns the cursor for the whole phase through its own `progress::CursorGuard` (`:616`, `:784`). A nested guard inside `runTasks` (`task_executor.cpp:103-104`) would show the cursor on destruction and leave it visible through the render phase. The field defaults to today's rule, so every existing call site stays byte-identical.
- **No *intended* behaviour change**: identical score table, identical comparison-video ffmpeg argv, identical bar text/roles/values, identical error strings and exit path, identical `win{i}.ts` paths, identical scratch-root guard, identical worst-window pick and `"Preview canceled by user."`. The deltas that were produced anyway are accepted and named in design.md's Risks.
- **The two scoring paths stay distinct.** `encodeprobe::measureWindow` sets `encodedHasLocalPts = true` (`encode_probe.cpp:314`) because its segment has already seeked; the two-input path calls `videoquality::measureSegmentQuality` directly (`preview_process.cpp:727`) and must not set it. Unifying the two would move the encoded-side seek in the scoring ffmpeg argv and change the measured scores.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is an internal reshape of preview's window bookkeeping. Every user-visible artifact is preserved: the same console output, the same comparison video, the same exit codes. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-media-item-and-stages` and `migrate-video-to-media-items`.

## Impact

- `src/preview/preview_process.cpp` — the item type, both bar sites on `runStage`, `WindowOutcome`/`WindowBatchResult`/`windowsCompleted` deleted, `encodeAndScoreWindow` writes into its item, `findWorstWindow` reads items, the `windows`/`segments` rebuilds
- `src/preview/preview_process.h` — the two per-completion bar texts become inline helpers (`windowProgressText`, `scoringProgressText`) so a test can pin them
- `src/core/media_item.h` — `StageSpec::hideCursor` returns as `std::optional<bool>`; `runStage` resolves it with `value_or(spec.progress != nullptr)`
- Tests: `tests/preview/preview_process_tests.cpp` gains one bar-text case per mode; no other test file is edited

**Depends on:** `unify-media-item-and-stages` (`runStage`, the concept, the item contract) and `migrate-video-to-media-items` (`StageSpec::setBarProgress`, the `progress != nullptr` cursor policy this change makes optional again).

**Explicitly out of scope:** the probe phase (`probeSingleInputPlan`, `encodeprobe::probeSingleFile`, `runProbeEncode`, `measureWindow`), the scoring algorithm and the `measureSegmentQuality` body, `preview_filtergraph` and `pickPreviewWindows`, output-path resolution, the scratch-root guard (`createPreviewProbeRoot`, `ProbeRootCleanupGuard`), the render phase (`renderPreview`, `buildPreviewCommand`, the score-table printing), and the temp-write helpers owned by the sibling change `share-partial-write-and-bar-helpers` — preview has no such site, so the two changes do not conflict.
