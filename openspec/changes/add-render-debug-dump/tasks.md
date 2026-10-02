## 1. Message-layer dump hook

- [ ] 1.1 Add cases to `tests/infra/terminal_tests.cpp`: with `ENCRO_DEBUG_DUMP_RENDER` unset or empty, `terminal::print`/`println` create no dump file; with the variable set to a temp path, an stdout `print`, an stderr `println` and another stdout `print` append three records `[out] …`, `[err] …`, `[out] …` in write order (drive the env with the existing `ScopedEnvVar`; verify red before the hook exists)
- [ ] 1.2 Implement the hook in `terminal::write` (`src/infra/terminal.cpp`): read `ENCRO_DEBUG_DUMP_RENDER` via `processenv::readNonEmptyEnvVar`, and when non-empty append `"[out] "+text+(newline?"\n":"")` (or the `[err]` form) with `std::ofstream(path, app | binary)` under one static mutex; mark the no-cross-process-locking ceiling with a `// ponytail:` comment — verify the 1.1 cases go green
- [ ] 1.3 Add a fidelity case to `tests/infra/terminal_tests.cpp`: the same message dumped under `configure(ColorMode::Always)` contains its escape sequences and under `configure(ColorMode::Never)` contains none, with `terminal::reset()` restoring state — verify it passes against the hook

## 2. Progress-bar final-state frames

- [ ] 2.1 Add a case to `tests/infra/progress_tests.cpp`: with the dump active (non-TTY test process, bars never drawn), build a `ProgressContext`, add one determinate and one indeterminate bar with known label text, `setProgress`, then `eraseBars`; assert the dump gains one plain-text frame line per bar carrying the determinate fill/percentage or the indeterminate marker and the bar's stored label text, while captured stdout stays free of bar text and cursor escapes — this asserts the non-TTY half of the frame scenarios directly; the TTY half is covered structurally (the append writes only the dump file) plus the existing suites staying green — verify red before the frame exists
- [ ] 2.2 Implement the frame append in `ProgressContext::eraseBars` (`src/core/progress.cpp`): after `cleared_ = true` and before the `progressBarsAllowed()` early return, append one plain-text line per bar built from stored state (`postfixText()` label slot raw and unfitted, `progressValue()`, indeterminate flag) through the same dump helper — verify the 2.1 case goes green

## 3. Verification

- [ ] 3.1 Run `xmake test-report` (full unit suite) and `openspec validate add-render-debug-dump --strict`; both green, and the pre-existing narration/capture cases confirm console output is unchanged with the env var unset

## Planning review

Fresh reviewer, planning-artifact stage (Coherence + Ground-truth), 2026-10-02; all findings fixed in the artifacts and confirmed resolved by an independent verifier pass.

- [major] Coherence — spec/tasks composed the frame from "prompt + stored postfix" but the proposal's state list had no prompt store → resolved: all artifacts now build the frame from the single label slot (`postfixes_`, seeded by `addBar`, overwritten by `setPostfixText`, exposed as `postfixText()`).
- [minor] Coherence — tasks said "fitted postfix" vs spec's "stored postfix text" → resolved: both say the raw stored label text; the fitted copy is a per-render view, not state.
- [minor] Coherence — the spec's TTY frame scenario had no covering task → resolved: tasks 2.1 pins the non-TTY half by assertion; the TTY half is covered structurally (the append writes only the dump file) plus the existing suites staying green.
- [major] Ground truth — design claimed "the `addBar` prompt is already kept on the context" as a distinct store → resolved: design states the single-slot reality with `src/core/progress.cpp:364` evidence.
- [minor] Ground truth — design claimed the stored postfix is "already width-fitted by `applyBarText`" → resolved: design says the label is dumped raw and why.
- [minor] Ground truth — tasks cited `tests/terminal_tests.cpp` / `tests/progress_tests.cpp`, which do not exist → resolved: `tests/infra/terminal_tests.cpp`, `tests/infra/progress_tests.cpp`.
- [minor] Ground truth — artifacts named `ScopedEnv`, the helper is `ScopedEnvVar` (`tests/test_utils.h:238`) → resolved.
- [minor] Ground truth — design's line citations drifted (`terminal.cpp:291`→293, `progress.cpp:21`→22) → resolved.
