# Design

## Context

See proposal.md — Why.

- The confirmation prompt lives inside the workflows, not in the batch driver: `confirmPicturePack` (`src/picture/picture_process.cpp`, used by both the direct pack and the compress path), `confirmEncodingStart` (`src/video/video_batch_execution.cpp`, the encode batch), and the resumed-encode packing prompt (`src/video/video_process.cpp`). All three call `readUserIpt(yesToAll, prompt)`, the single prompt primitive in `src/utils/utils.cpp`.
- `readUserIpt` returns false when a stop is requested, true immediately for `--yes` when no stop is pending, false when stdin ends or the answer is not `y`/`Y`, and true otherwise (empty input defaults to yes). The stop check comes first, so it also wins over `--yes`.
- `yesToAll` is part of `AppConfig`, and batch copies the base config per subfolder (`batch::subConfigFor`), so a per-subfolder config cannot carry an answer shared with the next subfolder.
- A declined prompt returns `canceledExitCodeForPromptAbort()` — 0 unless a stop was requested. That is why a declined subfolder currently lands in the batch's `done` bucket and the next subfolder asks again.
- `batch::runTasks` builds one `appctx::AppContext` per subfolder (`ctx.config = subConfigFor(...)`, `ctx.toolchain = request.toolchain`) and runs `pipeline::run(ctx)` sequentially. `AppContext` is neither copyable nor movable (`RuntimeContext` embeds a `std::shared_mutex`), so state shared across subfolders has to be reached through a pointer held by each subfolder's own context.

## Goals / Non-Goals

**Goals:**

- One confirmation per batch, asked by the first subfolder that reaches one, at the same pipeline point a single-directory run asks it.
- A decline ends the batch: the declining subfolder and every remaining subfolder are skipped, and the batch exits 0.
- No behavior change for single-directory runs, for `--yes`, and for prompt-free paths (pack-only, dry-run, media-less subfolders).

**Non-Goals:**

- Moving the prompt earlier (before scanning or probing) or prompting unconditionally before the first subfolder: the driver must not duplicate the workflows' prompt preconditions.
- Changing the exit code a declined single-directory run produces.
- Suppressing prompts for batch entirely — `--yes` stays the opt-out.
- Keying the shared answer by question text (see D2).

## Decisions

**D1 — The shared answer is a batch-owned `std::optional<bool>`, reached through a non-owning pointer in `RuntimeContext`.**

`appctx::RuntimeContext` gains `std::optional<bool>* promptConsent = nullptr;` (null for single runs). The batch driver owns the optional for the whole loop and assigns its address to each subfolder's context before running it.

- Alternative: a field in `AppConfig` — the config is copied per subfolder, so a stored answer would never reach the next one.
- Alternative: a process-global memo in utils — the unit suite runs many runs in one process, so a global would leak between runs and break the existing `[utils]` prompt tests.
- Alternative: a `std::shared_ptr` in `RuntimeContext` — explicit ownership, but the driver's local outlives every subfolder context, so it buys nothing.
- Include direction: `RuntimeContext` names only `std::optional<bool>*`, so `src/core` gains no dependency on `src/utils`.

**D2 — The stored answer is batch-level, not keyed by question text.**

A batch applies one task type, so a run normally sees a single question. The one case with two different questions is a re-run where an early subfolder resumes with all encodes complete (packing question, `video_process.cpp`) and a later one still needs encoding (encode question). Keying by question text would prompt twice there, contradicting the spec's "at most once"; the user already consented to the task type through the CLI flags, so one batch-level answer is the contract.

**D3 — A new wrapper keeps `readUserIpt` as the primitive.**

`bool readUserIptOnce(bool yesToAll, std::string_view prompt, std::optional<bool>* shared)` in `src/utils/utils.{h,cpp}`:

- a stored answer (when `shared` is set and has a value) is returned without printing or reading anything;
- otherwise it asks through `readUserIpt` and stores the answer — except when a stop request is pending after that call returns, because `readUserIpt` returns the same false for a stop that was pending before the prompt and for one that landed while the prompt waited, and that false is a cancellation rather than a consent decision; the driver's decline detection must stay out of the stop path;
- `--yes` still short-circuits inside `readUserIpt` (a pending stop wins over it), and storing the resulting true is harmless because yesToAll is uniform across a batch.

`readUserIpt` keeps its signature, its behavior, and its seven `[utils]` tests. The three prompt call sites switch to the wrapper (the picture helper takes the context instead of the config alone).

**D4 — Decline detection reads the shared answer, not the exit code.**

After `pipeline::run(ctx)` returns, `runTasks` checks the shared answer: a stored `false` on a run whose exit code is 0 means the run stopped at a confirmation — the subfolder is recorded as `Skipped` (its workflow already printed the decline notice), every remaining subfolder is recorded as `Skipped` with the notice style the stop path already uses (`Skipping <path>: confirmation declined.`), and the loop ends. A run that exited with the cancellation code keeps the existing stop path and is never reported as a decline; `batchExitCode` is unchanged, so no failed subfolder and no cancellation exit code yields 0 — the exit code a declined single-directory run produces.

**D5 — Testing seams stay the existing ones.**

Unit tests drive prompts through `testutils::ScopedCinBuf` (an `istringstream` as `std::cin`) plus `captureStdout`. The accepted path must not set `yesToAll` — that flag returns before the prompt is printed or read, so it can only prove the no-prompt case; the batch tests therefore drive the accepted path with stdin (one `y` line, which also proves the ask happened once, since a second prompt would read EOF and decline) and cover `--yes` as its own case. No new mocks or fakes.

## Risks / Trade-offs

- [A batch-level answer auto-answers a later, different question] → Documented in D2; the task type is fixed by the CLI flags, and the alternative re-introduces multiple prompts on a resumed batch.
- [A batch with no tty (piped stdin, no `--yes`) now stops at the first prompt instead of "completing" N do-nothing subfolders] → This is the intended fix: a run that cannot obtain consent must not report done. `--yes` remains the scripted path and is unchanged.
- [Per-subfolder skip notices for the remaining subfolders can be noisy on a large batch] → Mirrors the stop path's notices, which the batch spec already requires; the summary table stays the authoritative result list.
- [A raw pointer could dangle if a subfolder context outlived the batch local] → The driver owns the answer for the whole loop and every subfolder context is loop-local; the pointer is null for single runs.
