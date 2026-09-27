## Why

Preview is the last flow that still carries its own parallel-window machinery. Its single-input mode hand-builds one `taskexec::TaskSpec` per window, counts completions with its own atomic — `windowsCompleted` (`src/preview/preview_process.cpp:425`) is incremented only after a successful outcome write (`:451-452`) — and copies each result out through a per-index `WindowOutcome` vector. Its two-input mode scores windows in a plain sequential loop (`:730-788`) that drives the same bar with its own `done = index + 1` counter (`:765-778`). Neither site uses the item contract or `runStage`, so preview duplicates what `unify-media-item-and-stages` removed from picture and organize and `migrate-video-to-media-items` removed from video: the `TaskSpec` construction (`:431-467`), the completion counter, the outcome write-back and the per-completion bar update (`:453-463`).

Why now: the abstraction was built with preview's shape in mind. `unify-media-item-and-stages` D8 named preview's "draw no bar of my own" mode (`progress = nullptr` while painting through a captured `BarSlot`) as a case `runStage` must support, and `migrate-video-to-media-items` D5 recorded preview as the named next user. With preview migrated, the only flow left off the runner is pack, which is out by design.

## What Changes

- **One file-private item type for both modes.** `PreviewWindowItem` = `{index, Window, segmentPath, sourcePath, ItemOutcome}` with `id()`/`label()`/`source()`/`outcome()`, declared in `src/preview/preview_process.cpp`'s anonymous namespace (no new header). Single-input items carry `segmentPath = <probeRoot>/win<i>.ts`; two-input items leave it empty. `id`/`label` keep today's `"preview-window:{i}"` / `"window {i}"`, and `source()` returns the original path so the task records keep today's `input` attribute (`task_executor.cpp:36-43`).
- **Both bar sites run on `runStage`.** The single-input window batch (`encodeAndScoreAllWindows`, `:416-475`) keeps its concurrency (`clamp(maxParallelJobs, 1, windows.size())`) and loses its `TaskSpec` loop, its `windowsCompleted` counter and its bar update; the two-input scoring loop becomes the same stage with `maxConcurrency = 1`. Both run with `progress = nullptr` — `unify-media-item-and-stages` D8's "caller draws its own bar" mode — and a `postfix` that writes the bar's value and text directly, the shape video's overall bar already uses (`video_batch_execution.cpp:603-615`).
- **`WindowOutcome`, `WindowBatchResult` and `windowsCompleted` are deleted.** The `std::vector<Window>` that `FiltergraphSpec` (`preview_filtergraph.h:29-37`) and `printWindows` (`:203-229`) need is rebuilt from the items after the stage — the runner preserves input order — and `findWorstWindow(items)` returns the worst index from the items' `window.score` instead of a parallel outcomes vector.
- **No `StageSpec` field is added and `runStage` is untouched.** Preview already runs its window batch in `unify-media-item-and-stages` D8's "draw no bar of my own" mode (`.progress = nullptr, .hideCursor = false`, `preview_process.cpp:471-472`). Both migrated stages use the same mode, so the shipped policy `hideCursor = spec.progress != nullptr` (`media_item.h:157`) yields `false` on its own and the flow's phase-spanning `progress::CursorGuard` (`:632`, `:801`) stays the only cursor guard. Design D3 records why the field is not needed, so a later reader does not re-add it.
- **No *intended* behaviour change**: identical score table (pinned by the new five-window case), identical comparison-video ffmpeg argv, identical bar text and phase-relative values (pinned by helper cases), identical error strings and exit path, identical `win{i}.ts` paths, identical scratch-root guard, identical worst-window pick (strict `<`) and the shipped stop contract — one `Preview canceled by user.` notice and exit 130, pinned by `tests/preview/preview_process_tests.cpp:370`. The deltas that were produced anyway are accepted and named in design.md's Risks; the bar's roles have no read view in `ProgressContext`, so they stay read-verified.
- **The two scoring paths stay distinct.** `encodeprobe::measureWindow` sets `encodedHasLocalPts = true` (`encode_probe.cpp:314`) because its segment has already seeked; the two-input path calls `videoquality::measureSegmentQuality` directly (`preview_process.cpp:744`) and must not set it. Unifying the two would move the encoded-side seek in the scoring ffmpeg argv and change the measured scores.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is an internal reshape of preview's window bookkeeping. Every user-visible artifact is preserved: the same console output, the same comparison video, the same exit codes. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-media-item-and-stages` and `migrate-video-to-media-items`.

## Impact

- `src/preview/preview_process.cpp` — the item type, both bar sites on `runStage` with `progress = nullptr`, `WindowOutcome`/`WindowBatchResult`/`windowsCompleted` deleted, `encodeAndScoreWindow` writes into its item, `findWorstWindow` reads items, the `windows`/`segments` rebuilds
- `src/preview/preview_process.h` — the per-completion bar texts and the phase-relative value become inline helpers (`windowProgressText`, `scoringProgressText`, `phaseProgressValue`) so tests can pin them
- `src/core/media_item.h` and `src/core/task_executor.cpp` — untouched: no `StageSpec::hideCursor` field is added, and the shipped `hideCursor = spec.progress != nullptr` policy already yields `false` for `progress = nullptr`
- Tests: `tests/preview/preview_process_tests.cpp` gains one bar-text case per mode, one value-helper case and one score-list/`(worst)` case; no other test file is edited

**Depends on:** `unify-media-item-and-stages` (`runStage`, the concept, the item contract, D8's `progress = nullptr` mode) and `migrate-video-to-media-items` (the `progress != nullptr` cursor policy — `progress = nullptr` implies `hideCursor = false`, so the flow's own `CursorGuard` remains the only guard).

**Explicitly out of scope:** the probe phase (`probeSingleInputPlan`, `encodeprobe::probeSingleFile`, `runProbeEncode`, `measureWindow`), the scoring algorithm and the `measureSegmentQuality` body, `preview_filtergraph` and `pickPreviewWindows`, output-path resolution, the scratch-root guard (`createPreviewProbeRoot`, `ProbeRootCleanupGuard`), the render phase (`renderPreview`, `buildPreviewCommand`, the score-table printing), and the temp-write helpers owned by the sibling change `share-partial-write-and-bar-helpers` — preview has no such site, so the two changes do not conflict.
