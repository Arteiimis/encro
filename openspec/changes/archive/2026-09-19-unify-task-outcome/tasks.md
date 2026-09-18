## 1. The outcome shape (tests before the fill)

- [x] 1.1 Add `TaskState`, `TaskOutcome`, the reshaped `TaskRunResult` and `skippedCount()` to `src/core/task_executor.h`; verify `xmake build encro` fails only where callers still read `results`/`attempted`
- [x] 1.2 Update the six result-reading `[task-executor]` cases (the `resolveWorkerCount` clamp case is untouched) and add the new named case **"a slot the stop signal skipped is not a success"** (stop requested mid-run from a task body through `stopsignal::requestStop()`, guarded by `testutils::ScopedStopSignalReset`, with `maxConcurrency = 1`; synchronized on observable task state, no fixed sleeps); verify `xmake test-report --tag="[task-executor]"` fails before the executor fills outcomes (red first)
- [x] 1.3 Fill one outcome per slot in the worker loop (`has_value()` → `Succeeded`/`Failed`, skipped stays `Skipped`), keeping `attemptedCount` and `canceled` as they are; verify all `[task-executor]` cases pass
- [x] 1.4 Verify no fake-success path remains: `src/core/task_executor.h` declares no per-slot `attempted` marker (`rg -n "attempted" src/core/task_executor.h` matches only `attemptedCount`) and no per-slot `eh::Result` vector, and `TaskOutcome`'s default state is `Skipped`

## 2. Migrate the call sites

- [x] 2.1 `src/video/video_batch_execution.cpp` (`collectEncodingResults` `:437-453` and the summary at `:645`): filter on `state == Skipped` / `state == Failed`, keep the `attemptedCount` report; verify the video batch cases and the e2e encode flows stay green
- [x] 2.2 `src/pack/pack_service.cpp`: replace `attempted[index] == 0` with a `Skipped` check, replace `canceled && attemptedCount < plan.groups.size()` with `skippedCount() > 0`, and collapse `:311-316` into a single `state == Failed` check with the "left default-constructed (success)" comment deleted; verify `xmake test-report --tag="[pack-service]"` passes, including the throwing-task case at `tests/pack_service_tests.cpp:686`
- [x] 2.3 `src/video/encode_probe.cpp` (`:406`, `:849`, and the bounds-checked read at `:707` — the `outcomes.size() > taskIndex` guard may stay but must no longer be load-bearing); verify the `[encode-probe]` cases pass
- [x] 2.4 `src/picture/picture_compress.cpp:308-320` and `src/organize/pipeline.cpp:181-189`: keep both decisions exactly as they are (`canceled` still gates the phase result; organize still returns `!canceled`); verify the `[picture]` and organize cases pass
- [x] 2.5 `src/preview/preview_process.cpp:477` and `src/video/video_info.cpp:216,261` (result discarded): keep discarding; verify `rg -n "taskexec::runTasks" src` lists the same nine call sites as before the change

## 3. Verification & commits

- [x] 3.1 Run `xmake test-report` (full unit suite) and confirm zero failures, including the `[run_id]` attribute cases that read task states
- [x] 3.2 Run `xmake build e2e_tests && xmake run e2e_tests` and confirm the stop/resume end-to-end flows still behave (cancellation paths exercise `Skipped`)
- [x] 3.3 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 3.4 Run `xmake fmt` before committing; verify a second run produces no further changes
- [x] 3.5 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80); verify with `git log --oneline -2` that the split is docs-then-refactor
