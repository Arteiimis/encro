# Design: test-suite-cleanup

## Context

The audit (see proposal.md - Why) is complete and itemized: every deletion target was verified read-only against the current tree, including negative results (e.g. `stripCollisionSafePrefix` looks unreferenced outside its header but is called internally by `hasCollisionSafePrefix` / `collisionGroupPrefix` - it stays). The suites are green: 643 unit cases / 58 e2e cases. Both unit and e2e targets already include `tests/` on the include path, so hoisting helpers into `tests/test_utils.h` needs no build changes.

## Goals / Non-Goals

**Goals:**
- Every deletion is mechanically justifiable: the removed assertions are a subset of a surviving test's assertions (same production function, same branch).
- One shared home per helper; zero behavior change in the fake tool beyond deleting the one knob no test reads.
- The suite stays green at every commit (small, separable commits per group).

**Non-Goals:**
- No rewriting of surviving tests beyond what helper hoisting mechanically requires (no re-assertion, no re-tagging).
- No new test infrastructure capabilities (that is the follow-ups' business).
- No changes under `src/`.

## Decisions

- **Deletion standard**: a TEST_CASE is deleted only when another surviving TEST_CASE asserts the same production function, same branch, and every deleted CHECK/REQUIRE has an equivalent (or stronger) counterpart in the survivor. The two "疑似" borderline items from the audit (`packer_tests.cpp:105` delegation test, `logging_file_mgmt_test.cpp:315`) are deleted under this standard because their distinguishing assertions are unobservable from the public API; if review disagrees, dropping just those two deletions does not affect the rest.
- **`registerCapturingLogger` signature**: hoist the 7 identical copies as one `testutils::registerCapturingLogger(spdlog::logger&)`-style helper in `test_utils.h`; the json-file variant keeps its formatter difference as a thin wrapper in `logging_json_test.cpp` around the shared core. Alternative - a parameterized helper taking formatter - rejected: 7 of 8 callers would grow a parameter they do not care about.
- **File-creation helpers**: one `writeSizedFile(path, bytes, fill)` in `test_utils.h` (sparse-friendly where the platform allows); the 4 sparse variants and 6 exact/near copies collapse onto it. Naming follows the existing `writeTextFile` / `touchFile` family.
- **e2e/test_utils merge order**: merge the four uncontroversial items (`writeTextFile`, `listZipEntries`, `listRegularFiles`, delete-local-`ScopedEnvVar`) in one commit; keep `e2e::ScopedEnvironmentOverrides` as-is because its restore-to-unset semantics are strictly stronger than `testutils::ScopedEnvVar` - unifying it is a behavior-bearing change, deferred until needed.
- **Renames are isolated**: the 14 `*_test.cpp` → `*_tests.cpp` renames happen in their own commit (pure `git mv`), after content edits, so any later diff blames content not moves.
- **Verification of "no assertion lost"**: for each deleted TEST_CASE the surviving counterpart is named in the commit message; `xmake test-report` must show the same per-file assertion semantics (unit), and `xmake build e2e_tests && xmake run e2e_tests` must stay green.

## Risks / Trade-offs

- [A deleted "subset" test was actually pinning a side effect its survivor lacks] → The audit recorded the evidence per item; commit messages name the survivor, so any regression from deletion is bisectable to one commit and revertible independently.
- [Hoisted helper changes capture semantics subtly (e.g. logger sink flushing)] → The 8 copies were verbatim; hoisting is byte-equal code moved once, and the logging tests that exercise it run unchanged.
- [`ENCRO_FAKE_FFMPEG_PROGRESS_FRAMES` deletion breaks an out-of-tree consumer] → It is a test-only fake tool; no test uses the knob, and the fake tool ships only inside this repo's test targets.
- [File renames churn `git blame`] → Isolated `git mv` commit; blame follows renames with `--follow`.

## Migration Plan

Not applicable - test-only, no deployment surface. Rollback = revert the individual commit.

## Open Questions

None.
