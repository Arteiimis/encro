# Design: port-win-gated-tests-to-fake-tool

## Context

The migration is partially done already:

- `xmake.lua` wires `target("tests")` → `add_deps("encro_e2e_tool")` and injects `FAKE_TOOL_EXE_PATH` (absolute path define). The comment there says unit tests spawn the fake tool exe directly - the intended end state already existed at build level.
- `preview_process_tests.cpp` and `video/encode_probe_tests.cpp` implement the target pattern but inside `#if defined(_WIN32)` blocks, with hand-rolled `ScopedEnvVar` (`_putenv_s`, Windows-only) and `.exe`-hardcoded `copyFakeTool`.
- The other gated suites (`picture_compress`, `picture_process`, `pipeline_picture`, `video_process_orchestration`) still fake tools with inline `cmd.exe` batch scripts whose behaviors include: plain success/failure, parent-dir creation, partial-output-then-fail, call-count-dependent outcomes (`counterPath`), raw-command-line echo for argument assertions, `-progress` path emission, and multi-line output bodies.
- `fake_media_tool.cpp` already exposes ~23 environment knobs including exit codes, output bytes, stderr text, progress frames/padding, probe JSON file, scoring writes, delay/concurrency coordination, and an invocation log (`ENCRO_FAKE_TOOL_LOG_FILE`). Role detection is by argv[0] basename (`ffprobe` / anything else).

## Goals / Non-Goals

**Goals:**
- Finish and generalize the started migration so all ~45 gated cases run unguarded on both platforms.
- Single shared, portable set of test helpers instead of per-file duplicates.
- Fill the few real behavior gaps in the fake tool via its established environment-variable mechanism.
- Local verification of the Linux branch through system WSL before pushing.

**Non-Goals:**
- Any change under `src/` (production code has no platform gates in these paths).
- Running e2e tests under CI coverage (`--e2e`), adding a windows-latest coverage matrix, or chasing 100% on infra-level `_WIN32` branches (crash handling, console setup) - accepted dark spots.
- Reworking Catch2 execution model or enabling intra-process parallelism.

## Decisions

### D1: One native fake binary, copied per role - not cross-platform script generation

Rejected alternatives:
- *Cross-platform script generator (`#!/bin/sh` vs `@echo off`)*: two dialects to maintain forever, escaping pain (quoting rules differ radically), duplicates a fake that already impersonates ffmpeg faithfully (arg parsing, progress protocol, probe JSON, logging).
- *One binary, no role copies*: breaks ffprobe-vs-ffmpeg disambiguation which relies on argv[0] basename; copying is what the two already-ported files do and works unchanged on POSIX.

Copy semantics: destination name = requested basename + `.exe` suffix on Windows only; plain basename elsewhere (POSIX needs the executable bit, which `fs::copy_file` preserves from the source artifact).

### D2: Shared helpers live in `tests/test_utils.h`

Move from `preview_process_tests.cpp` into `testutils::` namespace:
- `ScopedEnvVar(name, value)` - portable body: `_putenv_s` on Windows, `setenv`/`unsetenv` (with prior-value capture) elsewhere; restore-on-destruct already correct.
- `copyFakeTool(dir, name) -> fs::path` - platform-suffix aware wrapper around `FAKE_TOOL_EXE_PATH` copy; hard fail if the define is missing so misconfiguration surfaces loudly, not silently.
Delete both per-file private versions. Files consumed patterns diverge slightly (vector-of-unique-ptr lifetime management) - standardize on storing `ScopedEnvVar` values directly in the case scope (destruction order = reverse declaration order is sufficient).

### D3: Fake-tool extensions stay environment-driven, scoped to four known gaps

Port inventory maps batch-script behaviors onto existing knobs except four:
1. **Per-invocation scheduling of delay and exit code**: static knobs cannot express what several ported cases need - cancellation-mid-batch requires call #2 to block in-flight yet ultimately succeed; pipeline retry cases require a delayed-then-failing call (exit 130) so stop signals can fire mid-delay. Generalize the budget idea into per-call-number schedules keyed by a caller-provided counter file (small plan segments like `<call>:<delayMs>:<exitCode>`; no matching segment = today's static behavior). The child records its invocation index there, so schedules work across the sequential spawns of one test flow.
2. **Progress-content suppression**: `writeFakeProgressFile` always emits `out_time_us=` when `-ss`+`-t` are present - which segmented encodes always pass - but the segment-end-fallback case asserts exactly the warning path that fires when `out_time_us` is ABSENT. Add a suppression knob for that field. While touching it: make `writeFakeProgressFile` create missing parent directories (the batch fakes did; the native one silently relies on pre-existing dirs).
3. **Exact-content output bodies**: default stance unchanged - relax plausibility-only assertions to existence/non-zero-size; where line content carries meaning, echo fixture files via the existing probe-side JSON-file pattern rather than inventing inline content generation.
4. **Argument-recording assertions**: superseded by the existing `ENCRO_FAKE_TOOL_LOG_FILE`; tests read back the structured log instead of parsing an echoed `%*` string. Note the log format is tab-separated (`toolName\targv…`) - space-based substring assertions from the batch era must be adapted at port time.

Deliberately NOT added: arbitrary-shell passthrough, templating languages, per-argument behavior tables - any new need should first try to fit the knob grammar above.

### D4: Port order minimizes risk

Pilot = `picture_compress_tests.cpp` (9 gated cases, smallest surface, exercises the budget knob immediately). Then `preview_process_tests.cpp` / `encode_probe_tests.cpp` (mostly deleting the guard once helpers are shared), then `picture_process` + `pipeline_picture` (counter pattern), then `video_process_orchestration` (progress + multi-line, most varied).

### D5: Platform-guard removal policy

After porting a file, `#if defined(_WIN32)` blocks disappear entirely from that file. If a case turns out to assert genuinely Windows-only observable behavior (expected count: zero; `rg _WIN32 src/{picture,preview,video,app}` shows no production gates there), it keeps its gate plus a comment naming the platform-specific contract.

### D6: Verification runs WSL-first

Per user requirement: before every push, the Linux branch is verified locally via the system WSL - configure/build/run the unit suite inside WSL (its own xmake cache/config; the repo is accessed via the same checkout). Windows suite stays green locally throughout (clang-cl build remains the primary dev loop). Push order: verify WSL → commit/push → confirm CI jobs (debug/release on ubuntu + coverage artifact delta against run 33002646675 baselines: preview_process.cpp 5.4%, picture_compress.cpp 3.7%, video_process.cpp 10.4%, app-entry/pipeline equivalents).

## Risks / Trade-offs

- [Behavior drift while translating batch semantics to knobs] → Port case-by-case against existing assertions; extend knobs rather than weakening assertions that encode real contracts (e.g., resume-retry counting); flag any assertion actually dropped in the change notes.
- [Env-var mutations leaking across cases hides bugs or flips results] → `ScopedEnvVar` restores previous values incl. unset-state; this matches how the already-ported files work today; no intra-process test parallelism exists.
- [Counter-file state races within a single process flow] → Scheduling knob reads sequential increments performed by children; same-file contention across concurrent children inherits the existing concurrency-coordination approach (`ENCRO_FAKE_FFMPEG_CONCURRENCY_DIR`) if needed - out of scope until a case demands it.
- [Windows-only code slips into portable blocks] → clang-cl locally green means little for POSIX; the mandated WSL pass compiles with gcc/clang before push, CI re-checks on ubuntu.
- [Coverage improvement overstated] → Expectation is qualitative: no orchestration source file below double-digit percentages; error-path coverage ceilings (cf. `encode_probe.cpp` at 87.5% despite being tested) persist.

## Migration Plan

Helpers land first (pure addition, both old and new patterns coexist), then per-suite ports land incrementally in D4 order, guards deleted as suites move. Each port is independently revertable. Rollback = restore gates on the affected file(s); no data/schema/process persistence anywhere.

## Open Questions

None blocking. Content-assertion relaxations are decided per case during application under the D3 default stance.
