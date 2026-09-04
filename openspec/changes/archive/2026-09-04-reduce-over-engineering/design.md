## Context

See proposal.md — Why for the full audit inventory. All changes are deletions or behavior-preserving restructures of zero-caller or duplicated code, verified repo-wide with `rg` during the audit. The repo has a strong regression gate: ~18k lines of unit tests plus a 4-shard parallel e2e suite (`xmake test-parallel`), so behavior preservation is checked by running the suites, not by argument.

## Goals / Non-Goals

**Goals:**
- Remove ~730 lines of verified-dead code, test-only API surface, duplicated helpers and scaffolding, across src/ and tests/, in 4 functional-area batches.
- Every batch lands with the full suite green.

**Non-Goals:**
- No behavior changes of any kind (CLI, file formats, job-state, progress, encoding/pack semantics). Any spec-level behavior change discovered mid-implementation aborts that item.
- No new tests for deleted zero-caller symbols (nothing to assert); deleted test cases are strict duplicates of surviving ones. The suite itself is the verification.
- include_cleaner plugin is explicitly out of scope (user decision).

## Decisions

- **Batch by functional area, one commit per batch** (per repo convention): D1 dead code (src), D2 pack API shrink, D3 stdlib/duplication cleanup, D4 test infrastructure. Each batch = 1 commit + its test adjustments; `git revert` of one commit rolls back a single area.
- **Deletion protocol per symbol**: re-run `rg "<symbol>" src tests` immediately before deleting (cheap, catches anything the audit missed); delete; let the compiler + suite catch the rest. Zero-caller claims are the contract — a symbol found alive at delete time is kept and reported.
- **Pack API shrink keeps tests compiling via the entry overloads**: `groupPackFiles*` tests move to `groupPackEntries*`/`buildDirectoryPackPlan`; `runPackPlan`/`runDirectoryPackWorkflow` tests move to `pack::execute(PackRequest)` (production pack-only path, pipeline.cpp:108, already covers directory mode). `zipNameStrategy` removal deletes the one test asserting it stays unset.
- **EncodeConfig.outputPath removal also removes its test** (encode_config_tests.cpp:221-247 asserts the fallback being deleted); the sibling tests covering `outputFilePath`/`tempOutputPath` survive unchanged. `ProgressData.status`, `mean()`, `isVmafLogEmpty()` and the other parser/quality deletions follow the same rule: delete the test-only assertions, keep the rest of the file.
- **`parallel` module folds into `task_executor.cpp`** as an internal function (its sole caller); the header + source are deleted outright. `resolveWorkerCount` stays the single place clamping worker counts.
- **`buildGroupOrdinalRanges` becomes one template** over `vector<vector<T>>` (either `fs::path` or `PackFileEntry` rows); the delegating wrappers are deleted, call sites call the template directly.
- **StdoutCapture/StderrCapture merge**: one `FileCapture` implementation parameterized on the `FILE*` (stdout/stderr), with `StdoutCapture`/`StderrCapture` retained as two-line aliases so the ~12 call sites and `test_utils_tests.cpp` self-tests need no renames. `shrink` wins over churn.
- **Reversed decision recorded**: `ENCRO_FAKE_FFMPEG_PROGRESS_PAD` (kept since `openspec/changes/archive/2026-08-28-test-suite-cleanup/`, "for mechanism contract") is deleted because no test sets it — same grounds as the already-removed PROGRESS_FRAMES. Drop the pad-filler block (the tool always emits `frame=10`; knob default pad=0 makes hardcoded output byte-identical). The remaining `ENCRO_FAKE_*` knobs keep progress emission env-configurable.
- **writeFile/touchFile migration is sed-assisted but reviewed**: before sed, `rg -n "writeFile\(|touchFile\(" tests` to confirm all hits are the test_utils.h helpers (no shadowing); after sed, the suite runs. ~150 mechanical call sites.

## Risks / Trade-offs

- [A deleted "zero-caller" symbol is actually reached from a path the audit missed] → Deletion protocol re-runs `rg` per symbol at apply time; compiler errors flag any leftover reference; full suite runs per batch.
- [Deduplicated helpers (guard, probeSide, ordinal template) subtly diverge from the originals] → Equivalence is enforced by the unit tests that target these paths (`encode_probe_tests`, `pack_service_tests`, e2e); each restructure lands as its own reviewable diff within the batch commit.
- [stdout capture merge breaks capture semantics (fd vs stream buffering)] → The merged `FileCapture` keeps the existing dup2-based mechanics verbatim, only the fd constant is parameterized; `test_utils_tests.cpp` self-tests + terminal/logging tests exercise both directions.
- [Sed migration of writeFile/touchFile hits a same-named helper elsewhere] → Pre-migration `rg` whitelist check (above) makes this a hard fail-fast, not a silent rewrite.
- [Deleting EncodeConfig.outputPath breaks a construction path that relied on the fallback] → Compile + suite catch it; the only recovery is re-adding the field (single-line), so rollback of the D1 commit suffices.

## Migration Plan

Per-batch: implement → `xmake test-report` → `xmake test-parallel` for cross-cutting batches (D2, D4) → commit (English, conventional). Order D1 → D2 → D3 → D4 so test-infra changes (D4) land last, after their call sites have already been cleaned up by the earlier batches. Rollback = `git revert` of the batch commit.

## Open Questions

None.