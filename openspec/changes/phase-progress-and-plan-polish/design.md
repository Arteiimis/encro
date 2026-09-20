## Context

See `proposal.md` — Why. The state that shapes the approach:

- Bars are drawn by `progress::ProgressContext` (`src/core/progress.{h,cpp}`), a wrapper over `indicators::DynamicProgress`. `eraseBars()` already exists and is called by exactly two phases today: the probe phase (`src/video/encode_probe.cpp:926`) and `organize` (`src/organize/organize_command.cpp:128`). The other bar-rendering phases — encode batch, pack (compact and full), picture compression, preview — never clear, so their frames survive the phase.
- The bar library's frame bookkeeping (`started_`, `total_count_`, `incomplete_count_`) is private and cannot be reset from outside, so a `print_progress()` after a clear would move the cursor up over output already written above the cleared block.
- Phase summary lines and their current homes: probe plan (`encodeprobe::printProbePlan`, `src/video/encode_probe.cpp:1059`), encode summary (`printEncodingSummary`, `src/video/video_process.cpp:494`), directory-pack success line (`src/pack/pack.cpp:491`), media-mode packing (no console line at all), picture compression (no console line at all), preview (`reportAndOpen`, `src/preview/preview_process.cpp:323`).
- The ETA badge's duration formatter is private to `progress.cpp`; the job-state store already persists per-task encode time for resume, but nothing exposes a phase wall time.
- Styling: `terminal::styled(stream, style, text)` + `terminal::roleStyle(Role)`; six roles, 16 palette slots, spans land on tokens only. `MessageKind::Summary` styles the leading verb `Good` automatically and bypasses the quiet gate; `MessageKind::Plain` is not suppressed by quiet (`src/infra/terminal.cpp:140`).

## Goals / Non-Goals

**Goals:**

- One rule for the whole CLI: a phase's bars live exactly as long as its work, and the phase leaves one line behind.
- Phase summary lines become the machine-readable record of a run: counts by outcome class, destination, elapsed time, and colour that matches the outcome.
- The probe plan block fits an 85-column budget and separates its totals from the table.

**Non-Goals:**

- No per-file permanent transcript lines (one line per finished file would fight the bar library's frame bookkeeping and is not what the request asks for).
- No change to the bar library, the ETA badge format, the postfix scroll grammar, or the organize phase — its report is its own product output and it already clears its bars before printing it (`src/organize/organize_command.cpp:128`).
- No new CLI flags, config keys, or persisted state.
- Not implementing the two-line narrow-terminal plan fallback (see Open Questions).

## Decisions

**D1 — Each phase clears its own bars; `taskexec::runTasks` is not the clearing point.**
The executor can be handed an external context owned by the phase (`EncodingProgressState::progressCtx`), and the encode phase has a monitor thread that must be joined before anything is cleared. So the clear belongs to the phase owner:

- encode: `runEncodingTasks` (`src/video/video_batch_execution.cpp:565`) clears after `execution.monitorThread.join()` (`:638`), so success, failure, and cancel exits all clear;
- pack compact: `PackService::packGroupsCompact` clears as soon as `runPackTaskPlan` returns — before its failure early-return, so cancel and failure clear too (`src/pack/pack_service.cpp:344`); pack full: `runPackTaskPlan` currently hard-codes `.progress = nullptr` (`src/pack/pack_service.cpp:297`) for both modes, so it takes a `ProgressContext*` parameter instead — `packGroupsFull` (`:376`) passes its own context and clears it when the plan returns, again before its failure early-return. Full-progress packing already renders one bar per archive through the executor's local context today; this only moves ownership so the phase has something to clear;
- picture compression: the bars' context lives inside `compressImageBatch` (`src/picture/picture_compress.cpp:255`, context at `:275`) and dies when it returns, so the clear happens at every exit of that function — not in `runCompressionPhase` (`src/picture/picture_process.cpp:546`), which never sees the context;
- video-to-WebP conversion: same shape in `picturewebp::runConversionPhase` (`src/picture/picture_video_webp.cpp:217`, context at `:242`), which clears at every exit — including the cancel branch that currently leaves the bar on screen;
- preview: before `printWindows` / `reportAndOpen` (`src/preview/preview_process.cpp:323`) in both input modes, including the render-failure exit.

Alternative considered: have `runTasks` clear the local context it creates when no context is passed. Rejected — it makes the shared executor own UI lifetime for one caller's benefit and still needs the encode path's own clear.

**D2 — A cleared context is finished: `render()` no-ops after a clear.**
`ProgressContext` records that it has been cleared; `render()` returns early and `renderable()` reports false from then on, so a straggling update cannot paint over already-written output. The library's bookkeeping cannot be reset from outside, and the failure mode (silently overwriting the lines above) is worse than the three lines this costs. A phase that wants bars after a clear uses a fresh context — which is what every phase already does.

**D3 — Each phase times itself; no shared clock service.**
Encode: `EncodingBatchOutcome.encodeElapsed`, measured inside `runEncodingTasks` around the batch (preparation + tasks + monitor join) — deliberately not the outer `ScopedTimer("video.encode")` block, which also covers probing and the confirmation prompt. Probe: measured in `runProbeStage` around `runProbePhase` and passed into `printProbePlan`. Pack: measured around `pack::execute` where the line prints (directory/picture modes measure inside `pack::execute`). Picture compression: measured around `runCompressionPhase` in `runCompressPackPhase`. Preview: measured around the run, passed to `reportAndOpen`.

**D4 — One duration formatter, two renderings.**
The badge's private `formatDurationPart` moves to `displaytext::formatDuration(std::chrono::milliseconds)` with the narration granularity (`24s` / `12m:34s` / `1h:05m`). The badge keeps its fixed-width `00m:24s` sub-minute form through a two-line wrapper in `progress.cpp`, because the badge scrolls and needs stable width while narration does not.

**D5 — Phase summary lines are built from styled tokens, printed as `Plain`.**
`MessageKind::Summary` would force a `Good` leading verb, but the verb's role now depends on the phase outcome (`Good` / `Warn` / `Bad`), and `terminal-color-palette` already defines the caller-styled-first-token path: a line whose first token carries its own role keeps that span and receives no leading-verb styling. So these lines print through `Plain` with explicit spans — which also keeps them out of the quiet gate (`Info`/`Success` are the only suppressed kinds). That matches how the run's result output already behaves under `--quiet`: the plan block and the preview written-to line print, while scan narration and announcements do not (`logging-behavior`). A small `terminal` helper (`text` + `Role` → styled string) keeps the call sites readable next to the existing `terminal::path` / `terminal::count`.

**D6 — The encode summary moves before packing.**
`printEncodingSummary(...)` moves ahead of `maybePackWorkflowOutputs(...)` in `runScannedEncodingWorkflow` (`src/video/video_process.cpp`; today the call at `:380` follows the packing call at `:375`). It depends only on the encode results map and the planned-output map, both available before packing, so the move is a reordering, not a re-plumbing. `setStage("completed")` stays after packing, where it is today.

**D7 — The 85-column budget is enforced against the row's real fixed width.**
`resolvePlanNameWidth` (`src/video/encode_probe.cpp:999`) takes a third bound: `85 - kRuleFixedWidth` (34), giving a 51-column name column at most. Note `displaytext::layoutColumns` budgets 32 fixed columns while a row actually spends 34 — the min() absorbs the 2-column discrepancy, so `layoutColumns` is left alone and the cap is expressed against the real number. Warning rows keep their marker inside the same budget (their name cell is truncated to `nameWidth - 2`).

**D8 — Blank line before the totals line, nothing else restyled.** `printProbePlan` prints one empty line before the `Total:` line; the collapsed single-line form is unaffected. The plan block's header row, file rows and totals line keep the plain rendering they have today — this change adds no bold or colour to the table itself.

**D9 — Scan start lines stop naming the root.** The video-scan start line (`Scanning input path for videos: {} ...`, `src/video/video_process.cpp:172`) and the directory-pack start line (`Scanning input path for files: {} ...`, `src/pack/packer.cpp:758`) drop the path, becoming `Scanning for videos...` / `Scanning for files...`; the multi-file variant (`Scanning input files for videos: {} file(s) ...`, `src/video/video_process.cpp:199`) aligns to the same wording (`Scanning for videos in 8 file(s)...`) so no start line restates what the completion line carries. The completion line keeps the root: it is the only line a piped run prints, and `pipeline-narration` requires it to name the root.

**D10 — Counts come from data the phases already have.** The encode line's skipped count is the batch's already-computed `skippedBeforeStart`, surfaced as `EncodingBatchOutcome.skippedCount`; the line's total is the results map plus that count, so `succeeded + failed + skipped` always equals the number of files the run was responsible for (probe-skipped files are absent from the results map today, which is why the total cannot come from the map alone). The failed count comes from the results map; the probe line's counts come from `plan.probed`. No new bookkeeping.

## Risks / Trade-offs

- **Bar clearing is invisible to the test suite** (bars render only on a TTY; every test captures piped stdout) → unit-test the context-level invariant (clear is idempotent, a cleared context renders nothing, `renderable()` is false) and cover the visible half — exactly one summary line per phase and the new ordering — with stdout assertions in unit and e2e tests; keep an explicit manual TTY check in `tasks.md` for the frame behaviour itself.
- **Durations make stdout non-deterministic** → printing functions take the elapsed value as a parameter, so unit tests pass a fixed value; e2e assertions match the shape (`in ` plus a unit suffix), never a value.
- **The verb's role change touches the quiet gate** → the summary lines print as `Plain` with caller-styled tokens; `Plain` is not quiet-suppressed (verified in `terminal.cpp:140`), so a `--quiet` run still prints the phase result lines, matching the existing plan block, encode count line and preview written-to line. A test asserts the quiet run keeps those lines and drops the narration.
- **Reordering the encode summary changes an e2e-visible sequence** → existing assertions match substrings, so they stay green; a new e2e assertion pins encode-before-pack.
- **`(n skipped)` on the encode line and `(n not probed)` on the probe line can be confused** → each class is worded to match its own plan-table row, which states the reason.
- **A longer pre-prompt block** (the probe summary line is added above the plan) → only one line is added, and the all-skipped batch keeps its single-line form.

## Migration Plan

None: no persisted state, no configuration, no CLI surface, no on-disk artifact depends on these lines. Rollback is reverting the commit.

## Open Questions

- `plan-output-formatting` still requires a two-line fallback for terminals too narrow for the minimum name column, while `printProbePlan` always renders the table (asserted by `printProbePlan keeps the table form on tiny terminals`). This change does not touch that requirement; resolving it — implement the fallback or drop the requirement — is a separate decision that changes neither this change's specs, approach, nor tasks.
