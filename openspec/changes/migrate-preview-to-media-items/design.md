## Context

See `proposal.md` — Why for the two sites and the machinery they duplicate. Read `unify-media-item-and-stages/design.md` D1-D3/D8 and `migrate-video-to-media-items/design.md` D5 first: this change consumes that contract and adds no new abstraction of its own beyond one `StageSpec` field.

Constraints that shape the approach:

- Preview's single-input batch is `encodeAndScoreAllWindows` (`src/preview/preview_process.cpp:416-475`): it builds `TaskSpec`s (`:431-467`), counts completions with `windowsCompleted` (`:425`, `fetch_add` at `:452`), writes `result.outcomes[index]` (`:451`) and drives the bar (`:453-463`). `WindowOutcome`/`WindowBatchResult` (`:347-354`) are the carriers.
- Its two-input scoring is `scoreComparisonWindows` (`:713-771`): a sequential loop with the same bar update (`:750-760`), no counter, no `TaskSpec`, and a direct `videoquality::measureSegmentQuality` call (`:727`).
- One bar spans three phases in each mode: probe 0-40% / windows 40-85% / render 85-100% (single-input, `:620`, `:648-666`) and probe 0-10% / scoring 10-85% / render 85-100% (two-input, `:787`, `:842-859`). The flow creates the bar and owns a `progress::CursorGuard` for the whole phase (`:616`, `:784`).
- `runStage` hardcodes `hideCursor = spec.progress != nullptr` (`src/core/media_item.h:157`); `task_executor` constructs a `CursorGuard` when `plan.hideCursor` (`src/core/task_executor.cpp:103-104`) and its destructor shows the cursor (`src/core/progress.cpp:564-570`).
- Bars and cursor escapes render only on an interactive, non-quiet stdout (`src/core/progress.cpp:20-23`, `:469-479`, `:540-545`), so no test in a redirected run can observe a bar frame or a cursor escape. No test pins a preview bar text today.
- `runTwoInput` (`:778`) already exceeds clang-tidy's 80-line `readability-function-size` threshold and is one of the current tidy baseline's warnings; the migration must not add diagnostics.
- `Window` and `fs::path` are copyable values, and preview's item has no runtime block: the pointer overload `migrate-video-to-media-items` D2 added is not a prerequisite here.

## Goals / Non-Goals

**Goals:**

- Both of preview's bar sites run on `mediaitem::runStage`; the per-item record, the completion counter and the outcome write-back exist once.
- Preview's item is a plain value, private to `preview_process.cpp`, with no new header and no pointer overload.
- Byte-identical console output, score table, comparison-video argv and exit paths.

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

It satisfies `mediaitem::Item` structurally, so no inheritance and no header: the type never leaves `preview_process.cpp`, and the only new declarations outside it are the two bar-text helpers in `preview_process.h` (D2). Both modes use the same type because the only difference is `segmentPath`; a second type would be the same five fields under another name.

The `std::vector<Window>` that `FiltergraphSpec` (`preview_filtergraph.h:29-37`) and `printWindows` (`:203-227`) consume is rebuilt after the stage by `windowsFromItems(items)` (and `segmentsFromItems(items)` for `renderPreview`'s encoded-side input list). `runStage` iterates the item vector in input order and never sorts (`unify-media-item-and-stages` D2), so the rebuilt vectors are element-for-element what the old code produced; the two-input mode reassigns its local `windows` the same way. `findWorstWindow` reads the items' `window` fields instead of a parallel `WindowOutcome` vector; its comparison stays strict `<`, so the first of two equal scores still wins and the `(worst)` marker cannot move.

Alternatives considered: keep `WindowOutcome` as `runOne`'s return payload and copy it back per index — rejected, it is exactly the per-index carrier the item replaces, and the runner already writes the outcome; a pointer item (`std::vector<PreviewWindowItem*>`) — rejected, the item is copyable and has no mutex/atomic to protect.

### D2: The bar value and text are owned by the flow's `postfix`

Both stages pass `setBarProgress = false` and a `postfix` that does two things: it sets the bar's value with the phase-relative formula, and it returns the per-completion text. The runner's own fraction cannot be used because the stage is the middle slice of a three-phase bar (`windowBase + (85 - windowBase) * done / total`), which is the same reason video added `setBarProgress` (`migrate-video-to-media-items` D5).

The texts are unchanged: `"Encoding windows: {}/{}"` for the single-input window batch and `"Scoring windows: {}/{}"` for the two-input scoring loop. Everything else about the bar stays in the flow: `probeSingleInputPlan`'s probe texts (`:555-585`), `"Windows encoded: {}/{}"` (`:653-656`), `"Rendering comparison video..."` (`:665-666`), `"Preview complete"`/`"Preview generation failed"`/`"Window encode failed"`, the roles, and `eraseBars()`.

No test pins a preview bar text today, and a bar renders only on a TTY (`progress.cpp:20-23`), so the migration would otherwise have no guard at all. The two format strings therefore become inline helpers in `preview_process.h` — `windowProgressText(done, total)` and `scoringProgressText(done, total)` — and the postfixes return them; `tests/preview/preview_process_tests.cpp` gains one case per mode asserting the exact strings. The runner side of the route is already pinned by `tests/media_item_tests.cpp:200-227` ("a postfix callback replaces the counter text entirely"), so helper case plus runner case cover the text from callback to bar.

The two-input helper's `BarSlot const* bars` parameter loses its optionality (`BarSlot const&`): the only caller always passes `&scoringBars` (`:838-839`), so the `if (bars)` branches were dead. The function is file-private and no test calls it.

### D3: `StageSpec::hideCursor` returns, because preview's bar outlives the stage

`runStage` currently equates "draws a bar" with "owns the cursor" (`media_item.h:157`). Preview is the counterexample: it passes a non-null `progress` (its bar is the stage's bar) while its own `CursorGuard` already covers probe, windows and render (`:616`, `:784`). With the hardcoded policy, `runTasks` would construct a nested `CursorGuard` (`task_executor.cpp:103-104`) whose destructor shows the cursor when the stage drains — emitting an extra show-cursor escape and leaving the cursor visible through the render phase, which is a user-visible regression, not an invisible ANSI delta.

The field's history is worth recording because it is the reason this is a decision and not an oversight. It was added as `bool hideCursor = true` by `dc73855` ("add the stage runner and its completion hook"), removed by `3c10bea` — the `unify-media-item-and-stages` post-change review, whose recorded reasoning was *"StageSpec loses hideCursor: every caller passed the default, so runStage now states the policy that a stage drawing a bar owns the cursor"* — and the policy became `spec.progress != nullptr` in `66b70c2` (`migrate-video-to-media-items`). Every caller did pass the default then; preview is the case that proves the equation is wrong for a bar that outlives the stage.

The fix is the smallest one that keeps every existing caller byte-identical:

```cpp
// StageSpec
// When unset, the stage hides the cursor iff it draws a bar. A flow whose bar
// outlives the stage owns the cursor itself and passes false.
std::optional<bool> hideCursor;

// runStage
.hideCursor = spec.hideCursor.value_or(spec.progress != nullptr),
```

Preview passes `hideCursor = false` at both sites. Alternatives rejected: accept the nested guard (its destructor leaves the cursor visible through the render phase); pass `progress = nullptr` and drive the bar from the postfix closure (the spec would lie about the stage owning a bar, and the flow would re-implement the bar write the runner already does); move the cursor guard into `runSingleInput`/`runTwoInput` only (that is what already happens — the nested guard is the problem).

The field's effect is terminal-only and untestable in a redirected run (`progress.cpp:540-545`), so its guard is the `value_or` resolution read at `media_item.h:157` plus the tidy/fmt bar, not a test case.

### D4: The two scoring paths stay distinct

`encodeprobe::measureWindow` scores with `.encodedHasLocalPts = true` (`encode_probe.cpp:314`) because its `win{i}.ts` segment has already seeked, so only the original is re-seeked; the two-input path calls `videoquality::measureSegmentQuality` directly (`preview_process.cpp:727`) without that flag, so the encoded input is seeked too (`video_quality.cpp:185-186`, `:407-408`). Routing the two-input path through `measureWindow` would change the scoring ffmpeg argv and therefore the measured scores. This is a decision, not a deferral: the two paths answer different questions about the same bytes, and the migration unifies the bookkeeping around them, never the work inside them.

Consequence for the item: `runOne` is mode-specific (encode-then-score vs score-only) while `runStage` owns everything around it — which is the split `unify-media-item-and-stages` D2 states.

### D5: Abort semantics stay exactly as they are

- **Single-input encode failure aborts the run.** `windowEncodeFailed` stays. It is set inside `encodeAndScoreWindow` (`:401`) before the error is returned, and the flow checks it after the stage (`:647-651`) to write `Role::Bad` + `"Window encode failed"`, erase the bars, and return `eh::makeError("Preview window encode failed.")`. The flag is deliberately not replaced by `stageResult.failed > 0`: a task that fails through an exception is converted to a `Failed` outcome by `runTasks` (`task_executor.cpp:52-62`) without setting the flag, so today the run proceeds to render in that case; switching to the stage's failure count would abort there. Keeping the flag preserves today's decision exactly.
- **`printFailures` is never called** — preview has no failure list, and the abort path is a single error string.
- **A two-input scoring failure stays a non-failure.** `runOne` logs the existing `LOG_WARN` and returns success, leaving `window.score` empty, exactly as the loop continues today; `printWindows` then prints `-` for that window.

### D6: `done` counts finished tasks including failed ones, and that is unobservable

The runner's hook fires once per finished task, so a failed window encode contributes to `done`, where `windowsCompleted` incremented only on success (`:452`). The only failure path overwrites the bar's text with `"Window encode failed"` and erases the bars before anything is printed, and bars render only on an interactive stdout (`progress.cpp:20-23`), so no pinned output and no test can show the differing frame. Accepted: the bar's final visible state and every printed line are unchanged.

## Risks / Trade-offs

- **`runTwoInput` is already over the function-size threshold** (`preview_process.cpp:778`, one of the tidy baseline's `readability-function-size` warnings). → The migration keeps its body the same size (the scoring loop moves into the stage, the call site stays) and the two stage functions stay well under 80 lines; the tidy task compares against the 127-warning baseline, and `xmake tidy -f preview` gives a fast per-TU check.
- **New log correlation on the two-input path.** → The scoring now runs on the executor, so records emitted inside it carry `task_id`/`input` (`task_executor.cpp:36-43`); the single-input path's equivalent records already did, and console output is unaffected. Accepted.
- **A stop request during two-input scoring now ends the dispatch early.** → `runTasks` stops handing out tasks once `stopsignal` is set, so later windows stay unscored (empty score, printed `-`) instead of being scored as today; the render still runs, as it does today. Accepted.
- **An exception escaping the scoring body becomes a per-item failure instead of propagating out of `runTwoInput`.** → The same class of delta `migrate-video-to-media-items` accepted for the verbose path (`task_executor.cpp:52-62`); the scoring helper returns `eh::Result`, so this is a latent path only. Accepted.
- **The bar-text guard is text-only.** → The value formula, `setBarProgress = false` and `hideCursor = false` remain pinned by reading, because the bar is TTY-only; the helper cases are the strongest available guard and the runner case covers the callback-to-bar route.
- **`findWorstWindow`'s tie-breaking is load-bearing.** → Keep the strict `<` so the first of two equal scores is still the `(worst)` window; a `<=` would silently move the marker.
- **The item is private to the `.cpp`.** → Nothing outside can reuse it; deliberate, because no other flow needs a preview window and a header would be surface without a consumer.

## Migration Plan

1. `StageSpec::hideCursor` (optional, defaulting to today's rule) + the `value_or` resolution in `runStage`; no caller changes. Additive and revertable alone.
2. `PreviewWindowItem`, `windowsFromItems`/`segmentsFromItems`, and the single-input batch on `runStage`; delete `windowsCompleted`; `encodeAndScoreWindow` writes into its item.
3. The two-input scoring loop on the same runner shape (`maxConcurrency = 1`); delete `WindowOutcome`/`WindowBatchResult`; `findWorstWindow` reads items.
4. The two bar-text helpers and one case per mode.
5. Full unit suite, e2e, `test-parallel`, `fmt`, `tidy`.

Rollback: step 1 is additive and independent; steps 2-3 are the visible ones and revert together with the type deletions; step 4 is a test-only guard. Steps 2 and 3 are independent per mode, so a regression in one can be reverted without the other.

## Open Questions

None. The decisions that could have changed the task breakdown — whether the item is a value or a pointer (D1), whether the two scoring paths merge (D4), and how the bar text becomes testable (D2) — are settled above.
