## Context

See `proposal.md` — Why for the six record types and the six pool+bar wrappers.

Constraints and existing contracts the abstraction must fit:

- `taskexec::runTasks` (`src/core/task_executor.h:58`) is the only parallel executor and already owns slot assignment, exception conversion, `Skipped` slots and cancellation (`src/core/task_executor.cpp:72-113`). The new runner is a caller of it, not a replacement.
- `taskexec::TaskPlan` has no completion hook, so every caller re-wraps each task to count (`organize/pipeline.cpp:155-175`, `preview_process.cpp:425,455-462`). `docs/backlog.md` ("Batch progress in `runTasks`") specified the narrow fix and deferred it until `unify-task-outcome` landed.
- `progress::ProgressContext` has no section/total/per-item API — a "section" is a `ProgressContext` a phase creates and `eraseBars()`es. The executor already forwards one shared context to every worker (`task_executor.cpp:84-85`). Each bar also has **one text slot**: `addBar(promptText, role)` seeds it and `setPostfixText` replaces it (`src/core/progress.cpp:361-366`, `:521-530`), so a bar's initial and lasting text are two different strings.
- `terminal::outcomeVerbRole(failed, skipped)` and `terminal::summaryCounts(succeeded, total, noun, failed, skipped, skippedLabel)` already exist (`src/infra/terminal.h:94-112`). The summary **formatting** is shared; what the four sites duplicate is the **counting** and the surrounding per-flow prose.
- Picture's compress phase persists **one** job-state task for the whole phase (`jobstate::makeCompressPhaseTask`, `picture_process.cpp:579-587`), not one per file. Organize uses no job-state at all. So per-item persistence is a per-flow property, not something the item model can assume.
- `progress::ProgressContext` has **one text slot per bar** — covered above; a bar's initial and lasting text are two different strings and `StageSpec` must supply both (D8).
- `pack::execute` takes `PackRequest` with grouping, naming and its own job-state wiring (`src/pack/pack.h:73-95`); `PackEntryInput` is its input type.

## Goals / Non-Goals

**Goals:**

- One per-item contract, one stage runner, one place where a parallel batch's bookkeeping lives.
- The runner owns every part of a stage whose *shape* is the same across flows; a flow owns every part whose shape is genuinely its own.
- Picture and organize migrate with **byte-identical** console output, identical planned paths, identical exit codes and identical resume decisions.
- The abstraction must fit video without rework even though video migrates later — design.md states the mapping.

**Non-Goals:**

- Migrating video, pack or preview (see proposal).
- **Unifying the skip predicates** — refuted, see D4. The four answer different questions and `cacheBackedByState`'s strictness is a deliberate anti-corruption guard for a shared cache directory.
- Changing progress percentage formulas: the five sites count five different units and `docs/backlog.md` records that the executor "cannot own a unit it is never told".
- Giving organize path-based resume.

## Decisions

### D1: The item is a concept, not a struct

```cpp
// src/core/media_item.h
namespace mediaitem {

enum class ItemState { Pending, Skipped, Succeeded, Failed };

struct ItemOutcome {
  ItemState state = ItemState::Pending;
  std::string failureReason;
};

template<class Ty>
concept Item = requires(Ty& item) {
  { item.id() } -> std::convertible_to<std::string>;
  { item.label() } -> std::convertible_to<std::string>;
  { item.source() } -> std::convertible_to<fs::path const&>;
  { item.outcome() } -> std::convertible_to<ItemOutcome&>;
};

}
```

The concept is checked against `Ty&`, not `Ty const&`: `outcome()` must hand back a **mutable** reference so the runner can record the result, while the rest are const members. The concept carries no `target()`: the shared layer never reads a planned output, so a flow that needs one keeps its own field. An earlier draft used `Ty const&` and would have rejected the very `MediaItem` D7 declares.

A single concrete struct cannot hold `chosenCq`/`totalFrames` (video, `app_context.h:72-98`), `contentHash`/`analysis`/`folderName` (organize, `organize_types.h:36-43`) and `entryName`/`originalEntryName` (picture) at once without a variant or an inheritance tree. Either would replace visible duplication with hidden indirection — a worse trade for a codebase whose stated goal is *less* complexity.

Alternatives considered: one `MediaItem` with a `std::variant` payload (rejected: every access becomes a visit, and the variant grows with every flow); a base class with virtual accessors (rejected: virtual dispatch on a struct that is copied and sorted by value, and it invites a god-base).

### D2: The split rule — uniform shape to the runner, per-flow shape to the flow

This is the decision that keeps the abstraction from becoming a god-function, so it is stated as a rule rather than left to judgement at each stage.

| The runner owns | The flow owns |
| --- | --- |
| the skip filter loop | the skip predicate itself (`alreadyDone`) |
| `TaskSpec` construction from `id`/`label`/`source` | the per-item work (`runOne`) |
| the `TaskPlan` and its concurrency | the concurrency value |
| the bar's per-completion update: `setProgress` and the counter text | the bar itself — `addBar` with the stage's own prompt, the closing text, `eraseBars` |
| the completion counter (via D3) | any percentage formula or postfix text |
| `outcome` write-back onto the items | — |
| failure collection, and `printFailures`, which the flow calls at the point its current print sits | — |
| the `StageResult` counts | the summary sentence, built from `StageResult` |
| — | the order in which its output is consumed, when that order is load-bearing |

The runner iterates the item vector in **input order and never sorts**. A flow whose *output* order is part of a persisted artifact or of the printed output sorts explicitly at its own boundary. Video is the case that matters: its failure list and its archive member order come from a path-sorted map today, and `migrate-video-to-media-items` carries that contract. The one order the runner does impose is on the failure list it hands to `printFailures`, which stays in path order because that is what the flows' `std::map<fs::path, string>` failure maps produced.

**Three refinements the migration forced, each to keep output byte-identical.**
The bar is created by the *flow*, not by the runner: picture's conversion phase
paints a live status line (`"Converting videos: 3/12 [encoding]"`) into the same
bar from inside its per-item work, so the flow needs the handle *before* the run
starts, and only `addBar` can give it. The flow also keeps the closing text and
`eraseBars`, because `setPostfixText` renders (`progress.cpp:365`) and the
existing order is closing text and then erase - a runner that erased its own bar
would swallow that frame. And the failure *print* is a helper
(`mediaitem::printFailures`) rather than part of `runStage`: the two picture
flows print at different points (conversion before its closing text, compress
after its sequential retry pass) and moving either would change the
interleaving, while `printFailures` still single-sources the format string.

If a stage needs a new callback beyond `alreadyDone` / `runOne` / the `StageSpec` text fields, it is not uniform and that stage stays out of the runner. Recorded so the next stage does not quietly add a sixth parameter.

### D3: `TaskPlan::onTaskFinished` is the enabling primitive

`TaskPlan` gains `std::function<void(std::size_t done, std::size_t total)> onTaskFinished`, called once per task that actually ran, after its outcome is recorded. The executor already tracks the count internally (`attemptedCount`, `task_executor.cpp:101`); this exposes it instead of making each caller wrap every task.

Thread-safety: the callback runs on worker threads. It is invoked outside the outcome write and must be cheap. `runStage`'s callback only touches the bar through `ProgressContext`, which is already mutex-guarded (`progress.h:141-167`), so no new lock is introduced. `done` is passed rather than left to the callback to increment, so the executor's atomic stays the single counter.

`done` counts tasks that have **finished**, so the executor increments that counter
after the outcome write instead of on entry to the task. A count of started tasks
would report the total before anything finished and make a caller's rate
(`done / elapsed`) wrong from the first completion, which organize's four workers
hit immediately. Nothing reads the counter before the pool drains, so its final
value - and therefore `TaskRunResult::attemptedCount` and `skippedCount()` - is
unchanged; a concurrent case in `tests/task_executor_tests.cpp` pins exactly that,
because a sequential one cannot tell the two semantics apart.

**The hook must not throw.** The worker loop routes every task through `runOneTask`'s try/catch (`task_executor.cpp:103`), but a hook invoked after the outcome write runs bare on a pool thread, and an exception escaping it terminates the process. It is therefore invoked inside a `try`/`catch (...)` that logs and swallows: the task's outcome is already recorded, and a failing progress callback must not lose it. That invocation lives in a free function (`notifyTaskFinished`) rather than inline in the worker loop, because the log fallback's `__FUNCTION__` expands to the enclosing lambda's name and the loop's cognitive complexity otherwise crosses clang-tidy's threshold.

Alternatives considered: have `runTasks` own the bar and the rate (rejected — `docs/backlog.md` records five different units and the `hideCursor`-without-`progress` case, where `runPackTaskPlan` reaches the executor with `progressCtx` defaulted to `nullptr` at `pack_service.cpp:280` while still passing `hideCursor = true` at `:307-312`, so the executor cannot own a unit it is never told); an `std::atomic_size_t` per caller (this is what exists today, and is what the hook removes).

### D4: `alreadyDone` is a parameter, not an item method

`runStage` takes `std::function<bool(Ty const&)> const& alreadyDone`. The predicates differ today and each is preserved verbatim:

- picture webp — `cacheBackedByState` (`picture_video_webp.cpp:44-54`)
- picture compress — the mtime check in `addCompressTask` (`picture_process.cpp:207-212`), which also builds the task list, so it runs before the stage and the stage is passed the resulting items
- organize — `item.analysis.has_value()`, equivalent to today's `cache.get(contentHash)` check (`pipeline.cpp:128`) because `applyCachedAnalyses` (`:104-113`) fills `analysis` from the cache before the stage runs. It is content-hash dedupe, not path progress.
- (video, later) — `jobstate::needsExecution` plus `actionTargetExists` (`job_state.cpp:643,647`)

**These four must not be unified, and that is a decision rather than a deferral.** They answer different questions:

| Predicate | Question it answers |
| --- | --- |
| `needsExecution` | has the state recorded this item as done? |
| `actionTargetExists` | is the planned artifact on disk? |
| `cacheBackedByState` | is a cached artifact *safely* reusable — state recorded Succeeded **and** the fingerprint matches? |
| picture mtime | is the temp artifact newer than its source? |

`cacheBackedByState` is deliberately the strictest: its own comment states why — the merge restores any Pending/Interrupted task whose target exists, so a cached file in the shared WebP cache directory may only be reused when the saved state explicitly backs it, otherwise a stale or truncated artifact gets packed. Folding the four into one predicate would either loosen that guard or tighten the other three; the mtime check is a cheap freshness test for a per-run temp directory and would be wrong as a state test, because the compress phase persists a single phase-level task rather than per-file records.

An earlier draft of this change treated the four as one duplicated rule to be collapsed later. The investigation refuted that, so no follow-up change exists for it and the seam stays per-flow. Making `alreadyDone` an item method would force the four to agree — which is exactly the merge rejected above.

### D5: The item does **not** own a `jobstate::TaskRecord`

An earlier draft had `record()` on the concept. It is dropped: picture's compress phase persists one phase-level task rather than per-file tasks (`picture_process.cpp:579-587`), and organize persists nothing, so both would carry a record they never write — dead state, and the abstraction would be paying for persistence it does not use.

Instead `id()` is the stable per-item identity, and a flow that persists sets its `TaskRecord::id` from it. The precedent already exists: `picture_video_webp.cpp:39-41` builds its conversion task id with `jobstate::makeEncodeTask(...).id`. `jobstate::TaskRecord` and its serialization are untouched by this change.

Alternatives considered: keep `record()` and have every flow carry one (rejected: dead state in two of four flows); reshape `TaskRecord` to split its video-only fields (`segmentIndex`, `resumeTimeUs`, `encodedMs`, `lastFrameCount`) into a sub-struct (rejected: a serialization and spec change for no gain in this change; recorded here so a later reader knows it was seen, not missed).

### D6: Stage sequencing stays explicit

A flow stays a function that calls its stages in order:

```cpp
auto items = scan(...);
auto const converted = runStage(convertSpec, items, ...);
auto const compressed = runStage(compressSpec, items, ...);
printSummary(compressed);
pack::execute(buildPackRequest(items));
```

No registry, no stage descriptors in a table, no ordering data. The seven reachable pipeline shapes have a fixed order (`src/app/pipeline.cpp:157-201`) and there is no consumer that composes them differently — a registry would be an abstraction for a requirement that does not exist.

### D7: `CompressTask` and `ConversionTask` collapse; `PackEntryInput` does not

Picture's two item types become one:

```cpp
struct MediaItem {              // new file: src/picture/picture_types.h
  fs::path sourcePath;
  fs::path outputPath;          // the temp/cache artifact, not the archive entry
  std::string entryName;
  std::string originalEntryName;
  mediaitem::ItemOutcome result;   // not `outcome`: that name is the accessor

  auto id() const -> std::string;      // jobstate::makeEncodeTask(sourcePath, outputPath).id
  auto label() const -> std::string;   // filename
  auto source() const -> fs::path const&;
  auto outcome() -> mediaitem::ItemOutcome&;
};
```

`src/picture/picture_types.h` is a **new file** (the directory currently holds only `picture_compress.*`, `picture_process.*` and `picture_video_webp.*`); it is one of the two files this change adds, the other being `src/core/media_item.h`.

`originalEntryName` is empty for conversions; `CompressResult` (`picture_compress.h:29-34`) disappears because the item carries the outcome. `id()` is the persisted conversion action id, `jobstate::makeEncodeTask(sourcePath, outputPath).id`; a source-path-only id makes two items that share a source and differ only in target ambiguous, which no flow does today. `PackEntryInput` stays pack's own type and is built from items at the boundary, as it already is (`picture_process.cpp:262-315`) — pack is a module, and forcing its grouped entries into the item model would be a cardinality change, not a unification.

### D8: Bar text is preserved by the flow's own prompt plus an optional postfix

A bar has **one** text slot: `addBar` seeds it and `setPostfixText` replaces it (Context). The initial and the lasting text are therefore two different strings in two of the four stages, and `StageSpec` cannot derive one from the other:

| Stage | `addBar` prompt | lasting text (`setPostfixText`) |
| --- | --- | --- |
| picture compress | `"Compressing: 0/12"` (`picture_compress.cpp:280`) | `"Compressing: 3/12"` (`:80`) |
| picture webp | `"Converting videos: 0/N"` (`picture_video_webp.cpp:249`) | `"Converting videos: 3/N"` (`:160`) |
| organize | `"Analyzing"` (`pipeline.cpp:149`) | `"3/5 - 12 img/s"` (`pipeline.cpp:172-175`) |
| probe (later) | `"Probing: 0/N files"` (`encode_probe.cpp:558`) | same shape |

So `StageSpec` carries the *bar the flow created* plus the text to write into it:

```cpp
struct StageSpec {
  progress::ProgressContext* progress = nullptr;  // null: draw no bar of my own
  std::size_t barIndex = 0;        // the bar the flow created with addBar
  std::string verb;        // used only by the default counter text
  std::string unit;        // "" or e.g. "files" -> "{verb}: {done}/{total} {unit}"
  std::size_t maxConcurrency = 1;
  // When set, replaces `verb` + count entirely on each completion, mirroring
  // setPostfixText's replace semantics.
  std::function<std::string(std::size_t done, std::size_t total, double elapsedSeconds)>
    postfix;
};
```

An earlier draft gave `StageSpec` a `prompt` field for the runner's own `addBar`
call. The migration dropped it: the flow has to call `addBar` itself (see D2, the
live-status case) with exactly the string it uses today, so a `prompt` field would
be the same text in two places.

With no `postfix` the runner formats `"{verb}: {done}/{total}"` (+ unit); with one it calls it, and the text the flow seeded the bar with is gone from that point on — exactly today's behaviour. Organize seeds the bar with `addBar("Analyzing")` and supplies a `postfix` returning `"{done}/{total} - {rate:.0f} img/s"`.

An earlier draft of this design claimed `"{verb}: {done}/{total}"` reproduced all four stages and called organize's rate "the `postfix` callback". Both cannot hold at once: a runner that formats `"Analyzing: 3/5"` never shows the rate, and one that calls the postfix never shows `"Analyzing"`. The bar's seeded text plus a `postfix` is what makes organize's output byte-identical.

`"Retrying: 0/N"` (`picture_compress.cpp:214`) is picture's **sequential** retry bar, not a `runStage` call (Migration Plan step 4), so it is outside this contract.

The **summary sentence is not the runner's**: the four sites differ in prose ("Encoded N/M videos → <dir> in <time>" vs "Compressed N/M pictures in <time>") and already share `outcomeVerbRole` / `summaryCounts`. The runner returns `StageResult{succeeded, failed, skipped, attempted, canceled}` and the flow formats.

`StageSpec` also accepts "draw no bar of my own" (`progress = nullptr`), because preview already runs a batch that way while painting through a captured `BarSlot` (`preview_process.cpp:453-462,468`) and because video's slot-bar layout ("Overall: i/N" plus one bar per worker slot, `video_batch_execution.h:84-144`) is not the single-bar shape the runner produces. A flow in that mode keeps its own `ProgressContext` and gets only the bookkeeping from the runner.

The bar-text contract needed one accessor to become observable at all: `ProgressContext::postfixText(barIndex)`, alongside the `progressValue` / `tickCount` / `barCount` / `cleared` views that already exist "for diagnostics and tests". Nothing else could read a bar's text (`postfixes_` is private), which is why no test in the repo asserted any bar text before this change - the contract was held by reading the code.

### D9: The migrated wrappers are deleted, not kept

`BatchState` (`picture_compress.cpp:29-36,275-351`), `ConversionBatchState` (`picture_video_webp.cpp:82-92,242-322`) and the inline block (`organize/pipeline.cpp:149-187`) are deleted. `CompactProgressState` (`pack_service.cpp:83-202`), `EncodingProgressState` (`video_batch_execution.h:84-144`) and `ProbeProgress` (`encode_probe.cpp:528-563`) stay, because pack, video and probe are not migrated here.

## Risks / Trade-offs

- **The callback count is the cost, and it does not pay for itself yet.** A migrated stage call site is ~15 lines of callbacks against a shared body of ~60, and the measured result is that migrating **two of four flows costs +95 source lines** (media_item.h +201, the picture and organize migrations -129, `db2f...`/`efc451a`): the three deleted wrappers save ~46 lines, which the runner's own 201 does not recoup. The tests add ~470 more. The design's earlier "150-200 lines saved" was wrong for this step; the payoff is the flows the abstraction was built for - video's five path-keyed maps and its two bundle types (`migrate-video-to-media-items`), which is also why D2's table is the piece to revisit if review judges the callbacks not worth it, and not the item contract, which video needs.
- **The runner could drift into a god-function.** → D2 states the rule explicitly and D9 lists what stays out; a sixth callback means the stage is not uniform.
- **Byte-identical console output is load-bearing, but the picture test files are not untouched.** `tests/picture/picture_process_tests.cpp` and `tests/picture/picture_compress_tests.cpp` assert on bar and summary text **and** construct `CompressTask`/`ConversionTask`/`CompressResult`, so D7's collapse necessarily edits their scaffolding. The rule is therefore: no asserted string changed - the one renamed case title names a type that no longer exists - while construction sites may. `tests/organize/pipeline_tests.cpp`, `tests/organize/stage_tests.cpp` and `tests/infra/progress_tests.cpp` stay unmodified.
- **Two accepted output changes, neither pinned by a test.** Converted clips now enter the archive in input order: the pack inputs are filtered from the item vector by `isPackable` (`picture_process.cpp`) and pack preserves input order, so the order moved from the old completion order to input order. And a compression that fails and is recovered by the retry pass is no longer listed among the failures, because `printFailures` reads the item's final outcome while the old flow printed its accumulating `std::map<fs::path, std::string>`. Both are intended.
- **`onTaskFinished` runs on worker threads and must not throw.** → invoked outside the outcome write, `done` supplied by the executor's atomic, invoked under a `try`/`catch (...)` that logs and swallows, and the only state it touches is already mutex-guarded (D3).
- **organize's `alreadyDone` is content-hash dedupe, not path progress.** → a cache hit is filtered by the runner and counted in `StageResult::skipped`. That also makes the bar's total the uncached remainder, matching today, where the total is `analysisTasks.size()` (`pipeline.cpp:152`) built from the uncached items. Organize's summary keeps reporting cache hits from its **own** counter (`:107-113`), not from `StageResult::skipped`, so the printed text is unchanged.
- **Ordering is load-bearing and the runner does not sort.** → D2 states the rule and prohibits a stage from relying on an order the runner does not produce; `migrate-video-to-media-items` carries the concrete case (path-sorted failure list and archive member order).
- **Two flows migrated, two not, leaves a mixed codebase.** → the mixed state is the point of doing picture and organize first; `migrate-video-to-media-items` is the named follow-up and design.md's D2/D3/D5 already state what video needs.

## Migration Plan

1. `TaskPlan::onTaskFinished` + its executor test (stop-mid-run and count-exactness cases), no caller migrated yet.
2. `src/core/media_item.h` with the concept, `ItemOutcome`, `StageSpec`, `StageResult` and `runStage`; a direct test over a throwaway item type pins filter → run → write-back → counts.
3. Picture webp conversion phase migrates first (smallest: one item type, one predicate, one bar).
4. Picture compress phase migrates, keeping the single `compress-phase` job-state task and the sequential retry pass (`picture_compress.cpp:191-250`) as it is; the retry pass keeps its own bar and is *not* a `runStage` call.
5. `CompressTask`/`ConversionTask` collapse into `MediaItem`; delete `CompressResult` and `BatchState`/`ConversionBatchState`.
6. Organize's analysis phase migrates; `ImageItem` gains `id()`/`label()`/`source()`/`outcome()`; the inline counting block is deleted.
7. Full unit suite, e2e, `test-parallel`, `fmt`, `tidy`.

Landed as two commits rather than one, per `AGENTS.md`'s "batch large working
trees by functional area": the completion hook and the runner first (`db2ffb1`,
additive, no caller migrated), then the two flow migrations (`efc451a`). The
planning artifacts were committed before either, as their own `docs:` commit,
and this design was reconciled with what shipped afterwards, because the
migration is what revealed the three refinements in D2 and the measured cost in
Risks.

Rollback: steps 1-2 are additive; 3-6 are independent per flow and revertable.

## Open Questions

None that change the specs, the approach or the task breakdown. The two decisions that could have (whether the item owns a `TaskRecord`, and whether the runner owns the summary sentence) are settled in D5 and D8.
