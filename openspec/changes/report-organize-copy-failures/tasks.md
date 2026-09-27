## 1. Tests

- [ ] 1.1 Add one `[organize]` case in `tests/organize/pipeline_tests.cpp`: a
      command-boundary run (`runOrganizeCommand` over an `ENCRO_FAKE_TAGGER`
      fixture with one image) whose `organized` path is a regular file, so the
      real copy path fails for every item; assert the captured report names the
      failed copy (`copy failed:` plus the image's source and destination) and
      that the run exits 0 (design D2). Verify with
      `xmake test-report --tag="[organize]"` that the report assertion fails
      against the current implementation while the exit-code assertion passes:
      the red is the missing wiring, and the exit-code half pins the decision
      that does not change.
- [ ] 1.2 Record the red-first run in this task list (command, observed failure
      line) before applying 2.1.

## 2. Implementation

- [ ] 2.1 Assign `.copyErrors = stats.errors` in the `ReportData` aggregate
      (`src/organize/pipeline.cpp:373-379`) and change nothing else; verify 1.1
      passes with `xmake test-report --tag="[organize]"`.

## 3. Verification & commits

- [ ] 3.1 `xmake test-report` (full unit suite) with zero failures.
- [ ] 3.2 `xmake fmt` twice with no second-run diff.
- [ ] 3.3 `xmake tidy` with no new diagnostics against the baseline count taken
      before the change (report-only task in this repo).
- [ ] 3.4 Commit the planning artifacts as their own `docs:` commit before the
      implementation, then implementation + test + ticked `tasks.md` in one
      `fix:` commit (English, subject < 72 chars, body wrapped at 80).
- [ ] 3.5 Archive the change with the spec sync (`openspec-archive-change`),
      committing the archived artifacts and the updated main spec.
