# Tasks

## 1. Ask-once wrapper (unit level)

- [x] 1.1 Add `[utils]` tests for the confirmation wrapper: a stored answer is reused without printing or reading (call it twice with different prompt texts and assert the second call returns the stored answer and writes nothing to stdout), the first answer is stored for later calls, no answer is stored while a stop request is pending (including one that lands during the prompt), and a call with no shared slot asks again every time; verify `build/windows/x64/release/tests.exe "[utils]"` fails on the missing wrapper and passes once 1.2 lands.
- [x] 1.2 Implement `readUserIptOnce(bool yesToAll, std::string_view prompt, std::optional<bool>* shared)` in `src/utils/utils.{h,cpp}` and add the batch-owned answer slot (`RuntimeContext::promptConsent`, null for single runs) in `src/core/app_context.h`; verify the `[utils]` tests pass and the seven existing `readUserIpt` cases are untouched.
- [x] 1.3 Route the three confirmation call sites through the wrapper — `confirmPicturePack` (taking the context instead of the config), `confirmEncodingStart`, and the resumed-encode packing prompt in `src/video/video_process.cpp`; verify `[utils]`, `[video-process]`, `[picture-process]` pass and the single-run prompt text is byte-identical (the e2e assertions on `do you want to encode the video to webp format` stay green).

## 2. Batch asks once and stops on a decline

- [x] 2.1 Add `[batch]` tests: a two-subfolder batch whose subfolders both reach the confirmation shows the prompt exactly once and marks both done (drive stdin through `testutils::ScopedCinBuf` with exactly one `y` line and no `yesToAll` — the flag returns before the prompt, and a second prompt would read EOF and decline, so both-done proves the single ask), a declined answer marks the declining subfolder and every remaining one skipped with exit 0 and one decline notice, a stop landing while the prompt waits keeps the stop path (exit 130, not a decline), and `--yes` prompts nothing; verify `tests.exe "[batch]"` fails before 2.2.
- [x] 2.2 Implement the driver change in `src/batch/batch.cpp`: one batch-owned answer assigned to each subfolder context, decline detection after each `pipeline::run` (record skipped, stop the loop, notice the remaining subfolders as skipped) that requires a zero exit code so a canceled run keeps the stop path; verify `[batch]` passes, a declined batch exits 0, and `batchExitCode` is unchanged for the existing failure and stop cases.
- [x] 2.3 Document the `batch` subcommand in `README.md`: a `### Batch processing (batch)` subsection under `## Usage` covering the contract the `batch-subfolder-tasks` spec defines — the positional root with one task type per run, task enumeration and its exclusions, the `<root>/packed/<subfolder>/` default with the `-o/--output` override and the rejected alias forms, the rejected option forms (`-I`/`--inputs`, `--state-file`) with the single-run validation rules, per-subfolder resume, media-less skips, failure isolation, stop behavior, the per-subfolder summary table, the exit-code rule, the once-per-run ffmpeg/ffprobe resolution, the once-per-batch confirmation, and `--yes` as the scripted mode — plus one Quick start example; verify every command in the section runs as written against a temp tree and that a two-subfolder run with one `y` on stdin prints the prompt exactly once while `--yes` prints none.

## 3. Integration checks

- [x] 3.1 Run `xmake test-report` (full unit suite) and the reporter probe `build/windows/x64/release/tests.exe -r console -s` (must report 0 failures) after the change.
- [x] 3.2 Run the full e2e suite (`xmake build e2e_tests && xmake run e2e_tests`) to confirm the existing batch cases (which pass `-y`) and the prompt-text assertions are unaffected.

## Workflow follow-up

- Run the code-review skill's code stage over the implementation diff, triage its findings, and record the outcome in this change's tasks.md.
- Archive the change after review and verification, syncing the delta into `openspec/specs/batch-subfolder-tasks/spec.md` inline per AGENTS.md.

## Planning review record

Planning-artifact review (code-review skill, planning stage) run after all four artifacts were written and before implementation: one independent reviewer, two lenses (Coherence, Ground truth), 7 findings, all accepted and fixed in the artifacts. A fresh reviewer then returned per-finding verdicts against the current text: 7/7 resolved. The reviewer was read-only (it verified claims against the repo source but ran no build or test), so the red/green claims of the tasks below remain unverified until implementation.

- C1 (Coherence, P1) — the design's accepted-path test seam used `yesToAll`, which returns before the prompt is printed or read, so that test could never show the single prompt tasks 2.1 asserted: design D5 and tasks 2.1 now drive stdin with one `y` line and treat `--yes` as its own no-prompt case. resolved.
- C2 (Coherence, P2) — `video-encode-probing` and `job-state-resume-matching` scenarios are falsified for batch subfolders after the first, with no delta in those capabilities: the batch requirement now scopes the rule to batch runs and leaves single-directory runs unchanged, and the proposal states why those capabilities stay unmodified. resolved.
- C3 (Coherence, P2) — the per-subfolder skip notice had no requirement behind it: the decline scenario now requires every remaining subfolder to be marked skipped with a notice naming it. resolved.
- C4 (Coherence, P2) — the proposal's wrapper test list and tasks 1.1 disagreed ("no reuse across separate runs" vs the stop-pending case): both now list the same four cases. resolved.
- G1 (Ground truth, P2) — the stop guard was pre-check only, while `readUserIpt` also returns false for a stop that lands during the blocking read, so a cancellation would have been read as a decline: the wrapper stores only when no stop is pending after the call, decline detection additionally requires a zero exit code, and a new scenario pins "a stop during the prompt is not a decline". resolved.
- G2 (Ground truth, P2) — "six `[utils]` tests" was wrong; the file has seven: both artifacts now say seven. resolved.
- G3 (Ground truth, P2) — the `--yes` precedence was inverted (the stop check precedes `yesToAll`): the design now states the stop check comes first and wins over `--yes`. resolved.

### Round 2 — README documentation scope

The scope addition (documenting the `batch` subcommand in the README) went through the same planning stage afterwards: one reviewer, two lenses, 2 findings, both accepted and fixed, then a fresh verification pass (2/2 resolved) that also raised one report-only P2, which was folded in as well.

- R2-1 (Ground truth, P2) — D6's premise that `## Usage` documents subcommand behavior was contradicted by `## Shell completion` (a subcommand documented in its own top-level section) and by the unlisted config subsection: D6 now names the config/`preview`/`organize` sections and explains why completion sits at the top level. resolved.
- R2-2 (Ground truth, P2) — tasks 2.3's README content list omitted spec'd contract elements (the `-o` override and rejected alias forms, the summary table, the exit-code rule, and the positional-root/one-task-type rules): both lists now describe the section as covering the contract the spec defines and enumerate those elements. resolved.
- R2-3 (Ground truth, P2, report-only, raised during verification) — the lists still omitted the rejected `-I`/`--state-file` forms with the single-run validation rule and the once-per-run toolchain resolution: both lists now name them, and the remaining minor spec details are covered by the lists' explicit pointer to the spec. resolved.

## Code review record

Code-stage review (code-review skill) run after implementation and self-verification, over the implementation diff since the last planning commit (`1a27173`): implementation `1c36e92`, review fixes `749b952`. The skill's tier classifier put it at **M** (confidence 0.97, mechanical 0.02), so two reviewers ran concurrently — Standards + Leanness in one context, Spec in the other — each with the repo, a shell and the full diff (pasted as `build/review-diff.txt`, 600 lines; its 11 `diff --git` headers omit only this change's own `tasks.md` checkbox update). `xmake fmt -k` passed for all 243 files before the review; clang-tidy reported 128 warnings with none on a line this diff adds (the two `33 * 1024 * 1024` widening warnings in `tests/batch/batch_tests.cpp` predate it, which the reviewer verified).

Findings: 9, all accepted, none rejected or deferred. All landed in `749b952`, and a fresh verifier then judged each against the current text: **9/9 resolved, none regressed**.

Standards:

- S1 (P1) — `tests/batch/batch_tests.cpp` reintroduced a local `countOccurrences` although `testutils::countOccurrences` already exists (`tests/test_utils.h:619`), against the testing rules' reuse rule: the local copy is deleted, the shared helper is used through a `using` declaration. resolved (749b952).
- S2 (P2) — `tests/test_utils.h` declared a scalar with a trailing return (`auto underflow() -> int_type override`), against the conventions' prefix style for scalars: now `int_type underflow() override`. resolved (749b952).
- S3 (P2) — design D5 still claimed the tests use only the `ScopedCinBuf`/`captureStdout` seams and "No new mocks or fakes", while the diff added `testutils::StopDuringReadBuf` and a dual-stream capture helper: D5 now names both and why they exist, and the proposal's matching phrase says "stream-capture seams". resolved (749b952).
- S4a (P2) — the driver's skip triplet was copied a third time: one `skip(reason)` lambda now serves the three sites, and the notice comes from the same format string, so the rendered lines are unchanged. resolved (749b952).
- S4b (P2, also the Leanness `delete` item) — the `declined` flag only cached `consent == false`: the flag is gone and both sites test the shared answer directly; the cancellation branch still comes first, so a stopped subfolder keeps the stop path. resolved (749b952).
- S4c (P2, also the Leanness `merge` item) — `makePromptingPackRequest` near-copied `makePictureCompressRequest`: a base `makePictureRequest` plus a compress flavour now share one body. resolved (749b952).
- S5 (P2, nit) — `<streambuf>` sat out of ASCII order in `tests/test_utils.h`, which clang-format does not sort: reordered. resolved (749b952).

Spec:

- SP1 (P2) — the README's decline sentence omitted that the subfolder that asked is skipped too: it now reads "the subfolder that asked and every remaining one are skipped, and the run exits 0". resolved (749b952).
- SP2 (P2) — the delta requirement's parenthetical named only the encode and resumed-packing prompts while the change also routes — and tests — the picture-packing prompt: the requirement now names it first. resolved (749b952).

Recorded but not findings: the reviewer's note that the driver's branch reorder is behaviour-preserving churn (kept on purpose: the decline branch requires a zero exit code), and its coverage limitation about the pasted diff's header count (explained above). The verifier also reported two items outside this change: pre-existing local `countOccurrences` copies in `tests/cmd_completion_install_tests.cpp` and `tests/e2e/encro_e2e_tests.cpp`, and that the delta requirement's description sits at 498 of the 500 characters `--strict` warns about, so lengthening it will trip that warning.

Verification runs after the fix commit: a rebuild (`tests.exe` relinked, since the commit's line-ending normalization had touched the sources after the first build) then `[batch]` 257 assertions / 37 cases, `[utils]` 102 / 30, the full unit suite at 17384 assertions, the reporter-mode probe with 0 failures, and the full e2e suite at 59 cases — all green.
