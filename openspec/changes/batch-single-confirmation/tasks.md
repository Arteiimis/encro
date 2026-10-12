# Tasks

## 1. Ask-once wrapper (unit level)

- [ ] 1.1 Add `[utils]` tests for the confirmation wrapper: a stored answer is reused without printing or reading (call it twice with different prompt texts and assert the second call returns the stored answer and writes nothing to stdout), the first answer is stored for later calls, no answer is stored while a stop request is pending (including one that lands during the prompt), and a call with no shared slot asks again every time; verify `build/windows/x64/release/tests.exe "[utils]"` fails on the missing wrapper and passes once 1.2 lands.
- [ ] 1.2 Implement `readUserIptOnce(bool yesToAll, std::string_view prompt, std::optional<bool>* shared)` in `src/utils/utils.{h,cpp}` and add the batch-owned answer slot (`RuntimeContext::promptConsent`, null for single runs) in `src/core/app_context.h`; verify the `[utils]` tests pass and the seven existing `readUserIpt` cases are untouched.
- [ ] 1.3 Route the three confirmation call sites through the wrapper — `confirmPicturePack` (taking the context instead of the config), `confirmEncodingStart`, and the resumed-encode packing prompt in `src/video/video_process.cpp`; verify `[utils]`, `[video-process]`, `[picture-process]` pass and the single-run prompt text is byte-identical (the e2e assertions on `do you want to encode the video to webp format` stay green).

## 2. Batch asks once and stops on a decline

- [ ] 2.1 Add `[batch]` tests: a two-subfolder batch whose subfolders both reach the confirmation shows the prompt exactly once and marks both done (drive stdin through `testutils::ScopedCinBuf` with exactly one `y` line and no `yesToAll` — the flag returns before the prompt, and a second prompt would read EOF and decline, so both-done proves the single ask), a declined answer marks the declining subfolder and every remaining one skipped with exit 0 and one decline notice, a stop landing while the prompt waits keeps the stop path (exit 130, not a decline), and `--yes` prompts nothing; verify `tests.exe "[batch]"` fails before 2.2.
- [ ] 2.2 Implement the driver change in `src/batch/batch.cpp`: one batch-owned answer assigned to each subfolder context, decline detection after each `pipeline::run` (record skipped, stop the loop, notice the remaining subfolders as skipped) that requires a zero exit code so a canceled run keeps the stop path; verify `[batch]` passes, a declined batch exits 0, and `batchExitCode` is unchanged for the existing failure and stop cases.
- [ ] 2.3 Document the `batch` subcommand in `README.md`: a `### Batch processing (batch)` subsection under `## Usage` covering the contract the `batch-subfolder-tasks` spec defines — the positional root with one task type per run, task enumeration and its exclusions, the `<root>/packed/<subfolder>/` default with the `-o/--output` override and the rejected alias forms, the rejected option forms (`-I`/`--inputs`, `--state-file`) with the single-run validation rules, per-subfolder resume, media-less skips, failure isolation, stop behavior, the per-subfolder summary table, the exit-code rule, the once-per-run ffmpeg/ffprobe resolution, the once-per-batch confirmation, and `--yes` as the scripted mode — plus one Quick start example; verify every command in the section runs as written against a temp tree and that a two-subfolder run with one `y` on stdin prints the prompt exactly once while `--yes` prints none.

## 3. Integration checks

- [ ] 3.1 Run `xmake test-report` (full unit suite) and the reporter probe `build/windows/x64/release/tests.exe -r console -s` (must report 0 failures) after the change.
- [ ] 3.2 Run the full e2e suite (`xmake build e2e_tests && xmake run e2e_tests`) to confirm the existing batch cases (which pass `-y`) and the prompt-text assertions are unaffected.

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
