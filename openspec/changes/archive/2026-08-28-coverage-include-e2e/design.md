# Design: coverage-include-e2e

## Context

The coverage plugin (`plugins/coverage/xmake.lua`) already implements `--e2e` (runs e2e under instrumentation, merges `e2e-%p.profraw` into `all.profdata`) and `--html` (single-binary HTML from `tests.exe`, which embeds all `src/` + `tests/` sources; multi-binary HTML is a known llvm-cov 22.x empty-output trap, see the earlier fix). CI (`ci.yml`) runs `xmake coverage --summary --keep --html` and uploads `build/coverage/html` as an artifact. The e2e-vs-unit audit produced a case-by-case duplication table (17 duplicate / 12 complementary / 28 unique / 1 misplaced harness self-test). Baseline numbers: src line coverage 82.79% (pure-src scope), with the gap concentrated in app entry (0-20%) and video orchestration (35-74%) - the layers e2e exercises on every run.

## Goals / Non-Goals

**Goals:**
- The published coverage number reflects all automated testing (unit + e2e) and nothing untestable (platform-bound code), so residual gaps are real signal.
- One-word CI change; no new plugin options; local reproduction is `xmake coverage --e2e --summary --html`.

**Non-Goals:**
- Not making `--e2e` the plugin default (local iteration on unit-only coverage stays cheap; the switch remains opt-in).
- Not touching production code to make platform layers testable (dependency-injecting console/signal/crash code buys number cosmetics, not safety).
- Not adding new e2e cases; pruning and relocation only.

## Decisions

- **CI opt-in, not default flip**: `ci.yml` gains `--e2e` on the coverage job. Alternative (default-on with `--no-e2e` escape) rejected: it taxes every local run for CI's needs, and the previous "unit-only is intentional" decision is reversed just as well by the CI flag.
- **Exclusion via the llvm-cov ignore filter, not source markers**: extend the existing ignore regex (which already excludes test sources) with the four platform-bound files (`src/infra/terminal.cpp`, `src/infra/stop_signal.cpp`, `src/infra/crash_runtime.cpp`, `src/infra/open_file.cpp`). Alternative (`// coverage-ignore` markers in sources) rejected: scattered annotations rot and leak into non-coverage tooling; a single regex in one config file is auditable. `app/prelude.cpp` / `app/app_entry.cpp` stay measured on purpose.
- **e2e pruning standard**: a case is deleted only when a named unit test exercises the same orchestration path with the same fake-tool injection and equivalent assertions (the audit table lists the pairing for each of the 18). The 12 complementary cases stay: their value is real-binary exit-code/CLI-glue, which in-process unit tests cannot assert. The harness self-test (`check-input` fake-ffprobe behavior) moves to the unit `[fake-tool]` section rather than being deleted - it is the registry of that knob's behavior.
- **HTML stays single-binary**: with `--e2e`, e2e counters merge into the shared profile by function-name identity (both binaries compile the same sources); the HTML generator keeps reading `tests.exe` + merged `all.profdata`, so the multi-binary empty-report trap is not reintroduced.
- **Verification**: before pushing, run `xmake coverage --e2e --summary --html` locally and record before/after per-file numbers for the target layers (app entry, video orchestration) in the change's tasks; CI confirms the artifact afterwards (dispatch run limited to the coverage mode via the existing `modes` input).

## Risks / Trade-offs

- [Merged-profile double counting if a function executes in both unit and e2e runs] → Counts add, they do not duplicate rows; llvm-cov semantics are additive across merged profiles, which is the intended "any test covered it" reading.
- [Pruned e2e case was the only place asserting X] → The audit's pairing table is the gate: deletion requires a named unit survivor per case; the table goes into the commit message.
- [Coverage number jumps confusingly between consecutive CI runs at merge time] → The change lands as one commit; the tasks record before/after numbers so the jump is explained in-repo, not in archaeology.
- [Windows-only Ctrl+C e2e cases skew runtime on Linux CI under instrumentation] → Out of scope: those cases are in the keep set and CI already gates them portably; instrumentation overhead is accepted this cycle and revisited only if the coverage job exceeds its budget.

## Migration Plan

Single commit series on one branch: plugin filter + CI flag + e2e pruning. Rollback = revert; the coverage job then reports the previous (unit-only) scope. No data migration.

## Open Questions

None.
