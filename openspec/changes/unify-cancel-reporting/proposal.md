## Why

A Ctrl-C is the one event a user is guaranteed to look for confirmation of, and today four stages report it wrongly or not at all: probing, video encoding and resumable packing print nothing at all, non-resumable packing, preview and organize print an `error:` line with exit 1 (organize keeps copying every remaining image before it even gets there), and a stop that kills a child can surface as that child's failure. Pictures already got this right for compression; the rest of the CLI has not.

## What Changes

- Every stage that a stop request aborts prints exactly one cancellation notice on stderr as `warning: <Stage> canceled by user.`, from the place that detects the stop: probing, video encoding, packing (resumable and non-resumable), preview (window encode, a render killed by the stop, and a probe killed by the stop) and organize (analysis and copy).
- A stop-aborted run ends with the cancellation exit code (130) instead of 1, so a wrapper script and the run summary both read "interrupted": non-resumable packing, preview and organize change from error-plus-1 to notice-plus-130. (Probing, encoding and resumable packing already exit 130; they only gain the notice.)
- No stage reports the stop's own victims as work failures: the killed compression children no longer print as `<path>: exit code NNN` (landed already), a killed preview render no longer reports `Preview generation failed (exit code 130)`, and a killed preview probe no longer reports `Probing skipped (short video or scoring failure)`.
- Organize honors a stop request during its copy phase, ending the run instead of copying every remaining image and reporting success.
- **BREAKING**: preview and organize cancellations now exit 130 rather than 1, and non-resumable packing exits 130 rather than 1. A wrapper branching on "non-zero" is unaffected; one matching exit code 1 specifically for a user cancel is not.

## Capabilities

### New Capabilities

- `cancellation-reporting`: the observable contract of a stop request — which stages abort, the single cancellation notice each one prints and on which stream, the exit code the run ends with, and the rule that a child killed by the stop is not reported to the user as work that failed.

### Modified Capabilities

- `pipeline-narration`: the abort rule currently allows a stopped phase to print nothing at all; the single cancellation notice replaces that allowance, and the phase still prints no summary line.

## Impact

- **Code**: `src/video/video_process.cpp` and `src/video/video_batch_execution.cpp` (encode-batch stop notice, probe abort notice), `src/pack/pack.cpp` (non-resumable stop check, one notice at the `execute(PackRequest)` funnel), `src/preview/preview_process.cpp` (notice and 130 on the three stop paths), `src/organize/{organize_command,pipeline,execute}.cpp` (notice and 130 on abort, stop check in the copy loop), `src/picture/picture_process.cpp` (compression cancellation, already landed in commit 79046a5).
- **APIs**: internal signatures only. `executeOrganize` gains a way to report the stop it hit (`ExecuteStats` or a return type change); `organize::runOrganize` may need to hand the command a distinguishable cancellation instead of the `interrupted: completed analysis is cached` error string. No CLI flag, config key or persisted-state change: a task the stop killed keeps the task status it has today, because resume relies on it.
- **Tests**: unit cases for the pack funnel (`tests/pack_execute_tests.cpp`), the probe and encode aborts (`tests/video/video_batch_execution_tests.cpp`), preview stop paths (`tests/preview/preview_process_tests.cpp`), organize copy abort (`tests/organize/`), the stale exit-code comment in `tests/app/app_entry_tests.cpp`, plus an e2e console-event case pinning the notice text and exit 130 for one representative stage (the `runEncroAsync` + `sendCtrlC` harness already exists) and a process-level case for the picture-compression cancellation that landed without one.
- **Specs**: the new `cancellation-reporting` capability and the `pipeline-narration` abort rule.
