## Context

See `proposal.md` — Why for the two sites and the machinery they duplicate. Read `unify-media-item-and-stages/design.md` D1-D3/D8 and `migrate-video-to-media-items/design.md` D5 first: this change consumes that contract and adds no new abstraction of its own.

Constraints that shape the approach:

- Preview's single-input batch is `encodeAndScoreAllWindows` (`src/preview/preview_process.cpp:416-475`): it builds `TaskSpec`s (`:431-467`), counts completions with `windowsCompleted` (`:425`, `fetch_add` at `:452`), writes `result.outcomes[index]` (`:451`) and drives the bar (`:453-463`). `WindowOutcome`/`WindowBatchResult` (`:347-354`) are the carriers. It already runs with `.progress = nullptr, .hideCursor = false` (`:471-472`) — `unify-media-item-and-stages` D8's "caller draws its own bar" mode.
- Its two-input scoring is `scoreComparisonWindows` (`:730-788`): a sequential loop with the same bar update (`:765-778`), no counter, no `TaskSpec`, and a direct `videoquality::measureSegmentQuality` call (`:744`).
- One bar spans three phases in each mode: probe 0-40% / windows 40-85% / render 85-100% (single-input, `:636`, `:675-683`) and probe 0-10% / scoring 10-85% / render 85-100% (two-input, `:804`, `:863-881`). The flow creates the bar and owns a `progress::CursorGuard` for the whole phase (`:632`, `:801`).
- `runStage` hardcodes `hideCursor = spec.progress != nullptr` (`src/core/media_item.h:157`); `task_executor` constructs a `CursorGuard` when `plan.hideCursor` (`src/core/task_executor.cpp:103-104`) and its destructor shows the cursor (`src/core/progress.cpp:564-570`). Both migrated stages pass `progress = nullptr`, so the policy yields `false` and no nested guard is created.
- Cancellation is shipped behaviour this change preserves. `reportPreviewCanceled` (`src/preview/preview_process.cpp:508-512`) erases the bars, prints one `warning: Preview canceled by user.` and returns `stopsignal::kCanceledExitCode`; nine checkpoints call it (`:539`, `:641`, `:667`, `:678`, `:815`, `:833`, `:861`, `:873`, `:912`). `openspec/specs/cancellation-reporting/spec.md` requires exactly one notice, exit 130, and no failure line for a stop's victim (the run requested the termination, so the stop is pending when the child's result is handled), and `tests/preview/preview_process_tests.cpp:370` (`[preview][stop-signal]`) pins the stopped probe, window-encode, render, first-probe and two-input-scoring shapes. The migration must keep the stop check inside the `windowEncodeFailed` branch (`:667`) ahead of the `Role::Bad` write (`:668-670`) and must not move or add a notice.
- Bars and cursor escapes render only on an interactive, non-quiet stdout (`src/core/progress.cpp:20-23`, `:469-479`, `:540-545`), but a bar's stored text and value are observable without a TTY (`ProgressContext::postfixText`/`progressValue`, `src/core/progress.h:102-123`), which is what the planned helper cases use. No test pins a preview bar text today.
- `runTwoInput` (`:795`) already exceeds clang-tidy's 80-line `readability-function-size` threshold and is one of the current tidy baseline's warnings; the migration must not add diagnostics (`xmake tidy` is report-only, so the bar is "no new diagnostics").
- `Window` and `fs::path` are copyable values, and preview's item has no runtime block: the pointer overload `migrate-video-to-media-items` D2 added is not a prerequisite here.

## Goals / Non-Goals

**Goals:**

- Both of preview's bar sites run on `mediaitem::runStage`; the per-item record, the completion counter and the outcome write-back exist once.
- Preview's item is a plain value, private to `preview_process.cpp`, with no new header and no pointer overload.
- Byte-identical console output, score table, comparison-video argv and exit paths — including the shipped cancellation contract (one notice, exit 130).

**Non-Goals:**

- Splitting or unifying the two scoring paths (see D4).
- Any change to the probe phase, the scoring algorithm, `preview_filtergraph`, `pickPreviewWindows`, output-path resolution, the scratch guard, the render phase or the temp-write helpers.
- A runtime-block split or a pointer item: change 3's D2 upgrade path is not needed by preview.

## Decisions

### D1: One file-private value item, one type for both modes

```cpp
// src/preview/preview_process.cpp, anonymous namespace
struct PreviewWindowItem {
  std::size_t index;
  Window window;          // the score lands here, exactly as the flow writes it today
  fs::path segmentPath;   // single-input: <probeRoot>/win<i>.ts; empty in two-input
  fs::path sourcePath;    // the original: TaskSpec.input and the failure line's path
  mediaitem::ItemOutcome outcome;

  auto id() const -> std::string;          // "preview-window:{index}"
  auto label() const -> std::string;       // "window {index}"
  auto source() const -> fs::path const&;  // sourcePath
  auto outcome() -> mediaitem::ItemOutcome&;
};
```

It satisfies `mediaitem::Item` structurally, so no inheritance and no header: the type never leaves `preview_process.cpp`, and the only new declarations outside it are the three bar helpers in `preview_process.h` (D2). Both modes use the same type because the only difference is `segmentPath`; a second type would be the same five fields under another name.

The `std::vector<Window>` that `FiltergraphSpec` (`preview_filtergraph.h:29-37`) and `printWindows` (`:203-229`) consume is rebuilt after the stage by `windowsFromItems(items)`, and the single-input render's encoded-side segment list is built inline at its one call site (the code-stage review inlined the single-use `segmentsFromItems` away). `runStage` iterates the item vector in input order and never sorts (`unify-media-item-and-stages` D2), so the rebuilt vectors are element-for-element what the old code produced; the two-input mode reassigns its local `windows` the same way.

**Where the score is written.** In both modes `runOne` writes the item: single-input `encodeAndScoreWindow` sets `item.window.metric = measured->value().metric` and `item.window.score = videoquality::percentile(measured->value().frameScores, 5.0)` (today's `outcome.metric`/`outcome.score`, `preview_process.cpp:410-412`) and returns `eh::Result<void>`, still setting `windowEncodeFailed` (`:401`) before an encode error; two-input `runOne` is the current loop body, writing the same two fields from `measureSegmentQuality` (`:744-757`) and returning success after the existing `LOG_WARN` when scoring fails (D5). A `runOne` that does not write `item.window` would print `-` for every window with no marker — that is the failure this states against.

**Where the worst index comes from.** After the stage, `windowsFromItems(items)` rebuilds the list and `findWorstWindow(items)` returns the index of the strict-minimum `window.score` (the first of equal scores, hence the strict `<`); the manual-range two-input mode never scores and keeps `worstIndex` `nullopt`, so no `(worst)` marker prints, exactly as today. `findWorstWindow` no longer takes a parallel outcomes vector; the single-input path (`:680`) and the two-input path both call the item-based overload.

Alternatives considered: keep `WindowOutcome` as `runOne`'s return payload and copy it back per index — rejected, it is exactly the per-index carrier the item replaces, and the runner already writes the outcome; a pointer item (`std::vector<PreviewWindowItem*>`) — rejected, the item is copyable and has no mutex/atomic to protect.

### D2: The bar value and text are owned by the flow's `postfix`

Both stages pass `progress = nullptr` — D8's "caller draws its own bar" mode, the same mode the single-input batch already uses (`preview_process.cpp:471-472`) — and a `postfix` closure that writes the flow's bar directly: it sets the value with the phase-relative formula and returns the per-completion text (the runner ignores the return when it draws no bar of its own, but the flow writes the text through its captured `BarSlot`). The runner's own fraction cannot be used because the stage is the middle slice of a three-phase bar (`base + (85 - base) * done / total`) — the reason video, which does pass `progress`, added `setBarProgress` (`migrate-video-to-media-items` D5).

The texts are unchanged: `"Encoding windows: {}/{}"` for the single-input window batch and `"Scoring windows: {}/{}"` for the two-input scoring loop. Everything else about the bar stays in the flow: `probeSingleInputPlan`'s probe texts (`:561-616`), `"Windows encoded: {}/{}"` (`:674-677`), `"Rendering comparison video..."` (`:683`), `"Preview complete"`/`"Preview generation failed"`/`"Window encode failed"`, the roles, and `eraseBars()`.

No test pins a preview bar text today. The stored text and value are observable without a TTY (`ProgressContext::postfixText`/`progressValue`, `src/core/progress.h:102-123`), so the migration guards both: the two format strings and the value formula become inline helpers in `preview_process.h` — `windowProgressText(done, total)`, `scoringProgressText(done, total)` and `phaseProgressValue(done, total, base)` — the postfixes call them, and `tests/preview/preview_process_tests.cpp` gains one case per text plus one value case. The runner side — the callback runs for a bar-less stage — is pinned by `tests/media_item_tests.cpp:262-287` ("a postfix runs for a stage with no bar of its own"), so helper cases plus that runner case cover the callback. What remains read-verified is what the API cannot show: preview's `ProgressContext` is a local in `runSingleInput`/`runTwoInput`, and `ProgressContext` exposes no role view, so the role writes and the live frames stay unpinned (see Risks).

The two-input helper's `BarSlot const* bars` parameter loses its optionality (`BarSlot const&`): the only caller always passes `&scoringBars` (`:857-858`), so the `if (bars)` branches were dead. The function is file-private and no test calls it.

### D3: No `StageSpec::hideCursor` field — both stages run in the "caller draws its own bar" mode

`runStage` equates "draws a bar" with "owns the cursor" (`media_item.h:157`). A stage that passed a non-null `progress` from preview would therefore get a nested `CursorGuard` (`task_executor.cpp:103-104`) whose destructor shows the cursor when the stage drains — an extra show-cursor escape and a cursor left visible through the render phase. The fix is not a new field: preview already runs its window batch in the mode that makes the field unnecessary. `encodeAndScoreAllWindows` passes `.progress = nullptr, .hideCursor = false` (`preview_process.cpp:471-472`), which is exactly `unify-media-item-and-stages` D8's documented case — `progress = nullptr` "because preview already runs a batch that way while painting through a captured `BarSlot`". Both migrated stages keep that mode and drive their bar through the `postfix` closure (D2), so `hideCursor = spec.progress != nullptr` already yields `false`, the flow's own `CursorGuard` (`:632`, `:801`) stays the only guard, and `StageSpec`, `runStage` and `task_executor` are untouched.

This is a non-decision, recorded so it is not re-litigated or re-added. The field's churn history: added as `bool hideCursor = true` by `aee6f4a` ("add the stage runner and its completion hook"), removed by `5b678e5` — the `unify-media-item-and-stages` post-change review, whose recorded reasoning was *"StageSpec loses hideCursor: every caller passed the default, so runStage now states the policy that a stage drawing a bar owns the cursor"* — and the policy became `spec.progress != nullptr` in `71f5412` (`migrate-video-to-media-items`). The first draft of this change proposed re-adding it as `std::optional<bool>` resolved with `value_or(spec.progress != nullptr)`; the review ruled that unnecessary, and D8's own wording supports the ruling — a stage with `progress = nullptr` means "draw no bar of my own", which is literally preview's case, and the flow owns both the cursor and the bar. Do not re-add it. The cursor's behaviour is terminal-only either way (`progress.cpp:540-545`), so the field was also untestable by construction.

### D4: The two scoring paths stay distinct

`encodeprobe::measureWindow` scores with `.encodedHasLocalPts = true` (`encode_probe.cpp:314`) because its `win{i}.ts` segment has already seeked, so only the original is re-seeked; the two-input path calls `videoquality::measureSegmentQuality` directly (`preview_process.cpp:744`) without that flag, so the encoded input is seeked too (`video_quality.cpp:185-186`, `:407-409`). Routing the two-input path through `measureWindow` would change the scoring ffmpeg argv and therefore the measured scores. This is a decision, not a deferral: the two paths answer different questions about the same bytes, and the migration unifies the bookkeeping around them, never the work inside them.

Consequence for the item: `runOne` is mode-specific (encode-then-score vs score-only) while `runStage` owns everything around it — which is the split `unify-media-item-and-stages` D2 states.

### D5: Abort and cancellation semantics stay exactly as they are

- **The shipped stop path is preserved.** `reportPreviewCanceled` (`preview_process.cpp:508-512`) erases the bars, prints one `warning: Preview canceled by user.` on stderr and returns `stopsignal::kCanceledExitCode` (130); the nine checkpoints (`:539`, `:641`, `:667`, `:678`, `:815`, `:833`, `:861`, `:873`, `:912`) implement `cancellation-reporting`'s rule that an aborted stage prints no summary and blames no killed child. The migration adds and moves none of them. In particular the stop check inside the `windowEncodeFailed` branch (`:667`) stays ahead of the `Role::Bad` write (`:668-670`), so a window encode the stop killed is reported as the cancellation, not as `Preview window encode failed.`; `tests/preview/preview_process_tests.cpp:370` (`[preview][stop-signal]`) stays green and is the pin for this contract.
- **Single-input encode failure aborts the run.** `windowEncodeFailed` stays. It is set inside `encodeAndScoreWindow` (`:401`) before the error is returned, and the flow checks it after the stage (`:664-672`) to write `Role::Bad` + `"Window encode failed"`, erase the bars, and return `eh::makeError("Preview window encode failed.")`. The flag is deliberately not replaced by `stageResult.failed > 0`: a task that fails through an exception is converted to a `Failed` outcome by `runTasks` (`task_executor.cpp:52-62`) without setting the flag, so today the run proceeds to render in that case; switching to the stage's failure count would abort there. Keeping the flag preserves today's decision exactly.
- **`printFailures` is never called** — preview has no failure list, and the abort path is a single error string.
- **A two-input scoring failure stays a non-failure.** `runOne` logs the existing `LOG_WARN`, leaves `window.score` empty and returns success, exactly as the loop continues today; `printWindows` then prints `-` for that window. A stop mid-scoring is different: `runTasks` stops handing out tasks, and the post-stage check (`:861`) reports the cancellation before the render (see Risks).

### D6: `done` counts finished tasks including failed ones, and the differing frames are TTY-only

`runStage`'s completion hook fires once per finished task, so a failed or stop-killed window encode contributes to `done`, where `windowsCompleted` incremented only on success (`:452`); the flow's `postfix` therefore writes one more step of `"Encoding windows: N/M"` for that task, where today's task lambda returns before the bar write once `encodeAndScoreWindow` fails. The encode-failure abort then overwrites the text with `"Window encode failed"` and erases the bars before anything is printed (`:668-670`); a stop mid-batch is likewise reported by the flow's post-stage check (`:667`, `:678`) after the bars are erased. Bars render only on an interactive stdout (`progress.cpp:20-23`), so the differing intermediate frame is not observable in a redirected run and no test pins it. Accepted: the bar's final visible state and every printed line are unchanged.

## Risks / Trade-offs

- **`runTwoInput` is already over the function-size threshold** (`preview_process.cpp:795`, one of the tidy baseline's `readability-function-size` warnings). → The migration keeps its body the same size (the scoring loop moves into the stage, the call site stays) and the two stage functions stay well under 80 lines; `xmake tidy` is report-only (`plugins/tidy/scan.py` counts diagnostics, it never compares a baseline), so the acceptance bar is "no new diagnostics" and `xmake tidy -f preview` gives a fast per-TU check.
- **New log correlation on the two-input path.** → The scoring now runs on the executor, so records emitted inside it carry `task_id`/`input` (`task_executor.cpp:36-43`); the single-input path's equivalent records already did, and console output is unaffected. Accepted.
- **A stop request during two-input scoring now ends the dispatch early.** → `runTasks` stops handing out tasks once `stopsignal` is set, so later windows stay unscored instead of each attempting a scoring probe. The flow's post-stage stop check (`:861`) then reports the cancellation before the render, so the score table and the written-to line never print and no window is shown as `-`. Accepted: the visible abort output is unchanged and the contract is pinned by `tests/preview/preview_process_tests.cpp:370`.
- **An exception escaping the scoring body becomes a per-item failure instead of propagating out of `runTwoInput`.** → The same class of delta `migrate-video-to-media-items` accepted for the verbose path (`task_executor.cpp:52-62`); the scoring helper returns `eh::Result`, so this is a latent path only. Accepted.
- **The bar guard is text and value only.** → The two texts and the phase-relative value are pinned by helper cases, and `tests/media_item_tests.cpp:262-287` pins that a bar-less stage's `postfix` still runs. The roles have no read view (`src/core/progress.h:102-123` exposes text/value/counters, not roles), and preview's `ProgressContext` is a local in `runSingleInput`/`runTwoInput`, so the role writes and the live frames are read-verified — that is the limit of the API, stated rather than papered over.
- **`findWorstWindow`'s tie-breaking is load-bearing.** → Keep the strict `<` so the first of two equal scores is still the `(worst)` window; a `<=` would silently move the marker. The planned five-window case pins it: the fake tool scores every window `96.0` (`tests/e2e/fake_media_tool.cpp:304`), so the marker must land on the first entry.
- **The item is private to the `.cpp`.** → Nothing outside can reuse it; deliberate, because no other flow needs a preview window and a header would be surface without a consumer.

## Migration Plan

The planning artifacts landed before implementation, in the shared `docs:` commits that planned both this change and `share-partial-write-and-bar-helpers` (`d2e18c8`, then `e08835f` for the planning review); sections 2-4 landed together in `0f7ef24` with the ticked tasks, and the code-stage review fixes in one more `refactor:` commit. The runner, `media_item.h` and `task_executor` are untouched by all of them.

1. Section 2 plus task 4.1's helpers, which the migrated postfixes call: `PreviewWindowItem`, `windowsFromItems` (with the render's segment list inline at its call site), the bar-text/value helpers, and the single-input batch on `runStage` with `progress = nullptr`; `encodeAndScoreWindow` writes into its item; `findWorstWindow` is pointed at the items; delete `windowsCompleted`, `WindowOutcome` and `WindowBatchResult`.
2. Section 3: the two-input scoring path on the same runner shape (`maxConcurrency = 1`), `runOne` writing `item.window.metric`/`score`, and `worstIndex` from `findWorstWindow(items)` after `windowsFromItems`.
3. Section 4's cases (4.2-4.4): the per-mode bar texts, the value formula and the score list/`(worst)` marker.
4. Section 5: full unit suite, e2e, `test-parallel`, `fmt`, `tidy` (no new diagnostics).

Rollback: step 1 introduces the item type, the rebuild helpers and the bar helpers, so step 2 reuses them and depends on it; dropping step 2 leaves the migrated single-input path and its guards intact (the two-input path keeps today's loop). Step 3 is guard-only and revertable alone. Section 1's stop-path checks are read-only and run after every step.

## Open Questions

None. The decisions that could have changed the task breakdown — whether the item is a value or a pointer (D1), whether the two scoring paths merge (D4), how the bar text becomes testable (D2), and whether the runner gains a cursor field (D3, no) — are settled above.
