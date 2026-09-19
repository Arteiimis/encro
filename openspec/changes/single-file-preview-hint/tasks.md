## 1. Tests

- [x] 1.1 Flip the `Compare:` assertion in the "post-encode summary is the count line plus preview hints on success" case (`tests/video/video_process_orchestration_tests.cpp:348`, 2 encoded files) to assert the hint is absent, correct that case's name and its "the preview hints follow" comment, and verify the case fails against the current implementation
- [x] 1.2 Leave the `Compare:` assertion in the "post-encode summary names failures and lists failed files" case (`tests/video/video_process_orchestration_tests.cpp:387`) unchanged — it has one success and one failure, so the hint stays and the case guards the single-success rule
- [x] 1.3 Add the `Compare:`-present assertion to the existing "handlePathEncoding encodes a single webp input through the orchestration path" case (`tests/video/video_process_orchestration_tests.cpp:74`): capture stdout in a `StdoutCapture` window, close the window, then assert on the read-back text outside it (an assertion inside the window is captured as reporter text — AGENTS.md's `-s` probe rule); verify the case passes both before and after the change, because it guards the single-file path the rule preserves
- [x] 1.4 Add a case for the resumed-run anchor (design D1): reuse the state-file setup of "handlePathEncoding resumes encode-only state and packs on pack-enabled run" (`tests/video/video_process_orchestration_tests.cpp:121`) reduced to a single input file, so the resume run recovers exactly one succeeded task and encodes nothing new; capture stdout the same way as task 1.3 (window then assert outside it) and assert the summary prints that task's hint; verify the case goes red if the guard counts this process's work instead of the summary's success count

## 2. Implementation

- [x] 2.1 Guard the hint loop at the end of `printEncodingSummary` (`src/video/video_process.cpp:545`) on the summary's `successCount` being exactly one (design D1/D2); verify tasks 1.1–1.4 pass with `xmake test-report --tag="[video-process]"`

## 3. Verification & commits

- [x] 3.1 Run `xmake test-report` (full unit suite) and confirm zero failures, verifying no other case depended on per-file hints
- [x] 3.2 Run `build/windows/x64/release/tests.exe -r console -s` and confirm 0 failures (the reporter-mode probe in AGENTS.md)
- [x] 3.3 Confirm the README encode section needs no change (it documents `preview` and probing, not the summary hint), and record the outcome in this task list
  - Outcome: the README documents the `preview` subcommand and the probe plan, but never the post-encode `Compare:` hint; no README edit needed.
- [x] 3.4 Run `xmake fmt` before committing; verify a second run produces no further changes
- [x] 3.5 Commit the implementation + tests + ticked `tasks.md` in one `fix:` commit (English, conventional, subject < 72 chars, body wrapped at 80); the planning artifacts are already committed as `3aabee9 docs: add openspec change single-file-preview-hint`, so do NOT re-commit them; verify with `git log --oneline -2` that HEAD is the fix commit and its parent is the docs commit
