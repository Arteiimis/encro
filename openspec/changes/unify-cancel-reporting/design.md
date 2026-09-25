## Context

See `proposal.md` — Why. The state that shapes the approach:

- The stop flag lives in `src/infra/stop_signal.cpp`: the console handler and `requestStop()` both set `gStopRequested` and signal one event, so every stage already polls one predicate. `app_entry.cpp` maps the run's exit code to `success`/`interrupted`/`failed` for the log summary, and `interrupted` is already keyed on the flag, so the exit code only has to become 130 for scripts and for the stage's own reporting.
- Where a stage detects the stop today: picture compression (`src/picture/picture_process.cpp`) and the video-to-WebP conversion (`src/picture/picture_video_webp.cpp`, whose cancelled `ConversionOutcome` the picture workflow maps to the code at `picture_process.cpp`) print their notices and end 130. The encode batch ends 130 too, but silently: `maybeHandleInterruptedEncoding` (`src/video/video_process.cpp`) returns the code and `runScannedEncodingWorkflow` returns before `printEncodingSummary`, so the notice a stopped encode prints today exists only on the declined-prompt path (`confirmEncodingStart`). Probing detects the stop in `runProbeStage` (`src/video/video_batch_execution.cpp`) and returns silently. `pack::runResumable` (`src/pack/pack.cpp`) returns `PackRunResult{exitCode = 130}` without printing while `runNonResumable` lets the "Packing canceled by user." error escape. Preview stops at one check inside `runSingleInput` and reports the stop as an error. Organize detects the stop in its analysis phase only, and its copy loop has no checkpoint at all.
- `pack::execute(PackRequest const&)` (`src/pack/pack.cpp`) is the single funnel for all four pack callers (picture direct, picture compress+pack, video media pack, pack-only).
- The e2e harness can deliver a real console event to a child (`runEncroAsync` + `sendCtrlC`, `tests/e2e/e2e_test_utils.h`), and skips when no console is attached; the unit seam for a stop is `stopsignal::requestStop()` plus the fake toolchain's gate files.

## Goals / Non-Goals

**Goals:**

- One predicate, one wording family, one exit code: every stop-aborted stage says so exactly once on stderr and the run ends 130.
- Keep the distinction between "the stop aborted this stage" and "this stage failed while a stop was pending", so a stage's failure detail is never replaced by a cancellation notice (the run-level summary keeps its existing rule that a pending stop reads `interrupted`).
- Make the picture-compression cancellation (already fixed) and every newly fixed path greppable in one spec, so the next stage added to the CLI has a rule to follow.

**Non-Goals:**

- Which cache or saved state survives a cancel: owned by `job-state-resume-matching`, `picture-compress-resume`, `picture-video-webp` and `image-character-organize`, and unchanged here.
- The confirmation prompts whose decline exits 0 today (`do you want to proceed with packing the pictures?`, the encode prompt): a decline is not a stop request, and its wording and exit code stay as they are.
- Guaranteeing a notice when the force-exit watchdog kills a run that never reached a checkpoint: the watchdog's whole purpose is to end a hung stage, so its output path stays best-effort, and the cancellation-reporting requirement says so explicitly. A pack-only run interrupted inside its single archive write is the case this covers today (see `docs/backlog.md`).
- The video workflow's pack step, which logs but prints nothing and returns 1 for a genuine packing failure (`src/video/video_process.cpp`, `maybePackOutputs`): a real failure, not a cancellation, and out of scope.

## Decisions

**D1 — The notice is printed by the stage that detects the stop, not by a central exit-code mapper.**
A `warning: <Stage> canceled by user.` line lands next to the stop check that already exists (probe: `runProbeStage`'s `Aborted` branch; encode: `maybeHandleInterruptedEncoding`, the one place the workflow learns the batch was cut short; packing: the `execute(PackRequest)` funnel, which sees the `PackRunResult` code; preview: its stop checks; organize: its command entry point). Alternatives considered: (a) print it once at the run teardown for any run ending 130 — guarantees once-only and covers future stages, but the teardown cannot name the stage without a new "cancel origin" registry on the app context, and it cannot tell a stop-aborted run from a run that failed while a stop was pending; (b) print it in the stage's error string and let the caller print — rejected, it forces every caller to parse text.

**D2 — A cancellation travels as a result, never as an error.**
Packing's non-resumable path mirrors what `runResumable` already does: when the pack plan fails and a stop is pending, it returns `PackRunResult{exitCode = kCanceledExitCode}` instead of the "Packing canceled by user." error. The `execute(PackRequest)` funnel then prints the single notice when the inner result carries that code, which is exactly once for all four callers, and a pack that completed before the stop (exit code 0) prints nothing even if the flag is set. Preview and organize return the cancellation code from their command entry points instead of an error. The decision is made by the stage that owned the work, at the point where it still knows whether the stop cut it short; the rejected alternative is having a *caller* re-derive a cancellation from a generic error string it cannot interpret (the picture workflow wrapping `Failed to pack pictures: ...`, the organize command inspecting `interrupted: ...`), which is what makes a stop look like a failure today.

**D3 — A killed child is the stage's abort, tested by the stop flag.**
The child's exit code alone cannot say whether encro killed it: a console Ctrl-C reaches the child directly, so the user's own run showed killed children exiting 255 and `-1073741510` beside the 130 that `subprocess-exec` synthesizes, and the preview render embeds whatever code it saw in its error text. So the stage checks `stopsignal::isStopRequested()` where it would otherwise report the child's failure — the same predicate `video_encode_runner.cpp` uses (`isStopRequested() || exitCode == kCanceledExitCode`), just without the code half, which would miss exactly the children the user's run reported. Every stop-abort in the codebase is decided this way (pack's two paths, preview, organize, the compression and conversion children); what this change adds is that the decision no longer reaches the user as the child's failure, and that no *caller* has to interpret an error string to find it. A genuine child failure that happens to coincide with a stop is therefore reported as the cancellation; the log keeps the child's reason and the durable task record is unchanged, and `subprocess-exec`'s "a stop after the child exited reports the real code, no spurious cancellation" rule is untouched. The preview probe additionally stops before it would print the "Probing skipped" warning or start encoding windows.

**D4 — Organize reports its abort explicitly and gains a checkpoint in the copy loop.**
`runOrganize` gains a `canceled` flag on its result (and `ExecuteStats` the same for the copy phase) instead of making the command re-derive the abort from the stop flag, so the two abort sites — analysis and copying — are the only ones that report a cancellation, and an analysis error stays an error. The copy loop checks the flag per image: a stop ends the run after the image in flight, leaving the already-copied files and the flushed analysis cache for the next run, which skips both. When the pipeline reports a cancellation the command prints `Organize canceled by user.` and returns 130 without printing the report.

**D5 — Notice wording follows the existing `<Stage> canceled by user.` family.**
New strings: `Probing canceled by user.`, `Packing canceled by user.`, `Preview canceled by user.`, `Organize canceled by user.`. The encode batch's stop path reuses the string its declined-prompt path already prints (`Encoding tasks canceled by user.`), and the picture stage notices keep their exact wording, so their tests and the e2e assertions stay valid. The prompt-decline paths print once and are not stop requests, so they neither gain a second notice nor change their exit code.

**D6 — The stage stops at its next checkpoint, not mid-operation.**
No stage is asked to abandon a half-written file: a stop takes effect before the next item starts, and in-flight children are terminated by `subprocess-exec` as they already are. Organize's copy loop therefore finishes the copy in flight (its staging-then-rename path already makes that atomic) and leaves the rest.

## Migration Plan

None: no persisted state, no configuration and no on-disk artifact changes, so rollback is reverting the commit. The one deployment-facing change is the exit code of a canceled preview, organize or non-resumable pack run (1 → 130), recorded in the proposal as breaking.

## Risks / Trade-offs

- **Preview and organize cancellations change exit codes (1 → 130), and non-resumable packing too** → documented as **BREAKING** in the proposal; nothing in `openspec/specs/` or the test suite pins the old codes, and a wrapper that treats any non-zero code as a failure is unaffected.
- **A notice printed by a stage can be missed by a future stage** → the requirement is per-stage and the specs list the stages explicitly, so a new stage that skips it is a spec violation the review can catch; the alternative (D1a) buys once-only at the cost of a registry and of mislabeling coincident failures.
- **Unit tests can only raise the stop in-process, while the user's Ctrl-C arrives as a console event** → the flag, the event and the exit path are the same after the handler runs (`src/infra/stop_signal.cpp`); keep the unit seam as the primary guard and add one e2e console-event case for a representative stage, which skips where no console is available.
- **A stop during organize's copy phase now leaves a partially organized directory** → the run already resumes from the analysis cache and skips identical existing files, so re-running the same command completes the copy; no new state is introduced.
- **`--quiet` runs still print the notice** → intended: severity diagnostics and cancellation notices are outside the quiet gate (`terminal::quietSuppresses`), and `console-output-conventions` puts them on stderr either way.
