## 1. Tests

- [x] 1.1 Add one `[organize]` case in `tests/organize/pipeline_tests.cpp`: a
      command-boundary run (`runOrganizeCommand` over an `ENCRO_FAKE_TAGGER`
      fixture with one image) whose `organized` path is a regular file, so the
      real copy path fails for every item; assert the captured report names the
      failed copy (`copy failed:` plus the image's source and destination) and
      that the run exits 0 (design D2). Verify with
      `xmake test-report --tag="[organize]"` that the report assertion fails
      against the current implementation while the exit-code assertion passes:
      the red is the missing wiring, and the exit-code half pins the decision
      that does not change.
- [x] 1.2 Record the red-first run in this task list (command, observed failure
      line) before applying 2.1.

      Red-first run before 2.1 (`MSYSTEM= xmake test-report --tag="[organize]"`):
      `test cases: 52 | 51 passed | 1 failed`, `assertions: 286 | 285 passed |
      1 failed`; the failing assertion is
      `tests/organize/pipeline_tests.cpp(410): FAILED: CHECK( captured.find(
      std::format("copy failed: {} -> {}", source.string(),
      destination.string()) ) != std::string::npos )`. The same case's `copied 0`
      and exit-code 0 assertions passed, so the red is the missing wiring only.

## 2. Implementation

- [x] 2.1 Assign `.copyErrors = stats.errors` in the `ReportData` aggregate
      (`src/organize/pipeline.cpp:373-379`) and change nothing else; verify 1.1
      passes with `xmake test-report --tag="[organize]"`.

## 3. Verification & commits

- [x] 3.1 `xmake test-report` (full unit suite) with zero failures.
- [x] 3.2 `xmake fmt` twice with no second-run diff.
- [x] 3.3 `xmake tidy` with no new diagnostics against the baseline count taken
      before the change (report-only task in this repo).

      Verification after 3ec5c7f: full suite `test cases: 821 | 810 passed | 11
      skipped`, `assertions: 16361 | 16361 passed | 0 skipped`. `xmake fmt` run
      twice: both runs produced the same diff
      (sha256 c3f781aaf29284f626bac287122712da9a0354fb3aa4b812dc1cc99b4828d051),
      whose only entries are the pre-existing clang-format drift in
      `src/cmd/cmd.cpp` and `src/cmd/help_layout.h` (reverted, out of scope);
      nothing under `src/organize/`, `tests/organize/` or the change directory
      differed. `xmake tidy`: 132 warnings before and 132 after, the only
      diff being four pre-existing `bugprone-unused-return-value` diagnostics
      in `tests/organize/pipeline_tests.cpp` shifted one line by the added
      include; no new diagnostic.
- [x] 3.4 Commit the planning artifacts as their own `docs:` commit before the
      implementation, then implementation + test + ticked `tasks.md` in one
      `fix:` commit (English, subject < 72 chars, body wrapped at 80).
- [ ] 3.5 Archive the change with the spec sync (`openspec-archive-change`),
      committing the archived artifacts and the updated main spec.
