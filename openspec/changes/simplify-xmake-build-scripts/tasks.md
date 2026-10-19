# Tasks

## 1. Root build script

- [x] 1.1 Replace the nine per-directory `add_files` lines of the `tests` target with `add_files("tests/**.cpp|e2e/**")`, and verify with `xmake lua -c "import('core.project.config'); import('core.project.project'); config.load(); local t = project.target('tests'); local n = 0; for _, f in ipairs(t:sourcefiles()) do n = n + 1 end; print(n)"` that the target still reports 80 source files (the same set the per-directory lines produced).
- [x] 1.2 Evaluate a `phony` dependency target for the package/syslink/include list `e2e_tests` repeats: its public propagation works (`xmake show -t e2e_tests` attributed `-> dep(...) -> packages` to it and `xmake build e2e_tests` linked), but with one dependent it holds a copy of `encro_core`'s list rather than a share — dropped in code review (see L1 below) and `e2e_tests` keeps its own list.

## 2. `test-report` follows the configured build

- [x] 2.1 Load the project configuration at the start of the task (`config.load()`), replace the `platform`/`arch`/`mode`/`ext`/`bin_dir` locals with `path.absolute(project.target(<name>):targetfile())` per suite, and verify `xmake f -m debug -y; xmake test-report --unit-shards=1` builds and runs the debug `tests.exe` (the run's own reported binary path and the `--selftest` line name the debug directory) with the report still landing at the documented `build/last-test-report.xml`, then restore `xmake f -m release -y`.
- [x] 2.2 Add the `--selftest` checks for the loaded configuration reporting a mode and a platform and for the resolved binary path being absolute, and verify `xmake test-report --selftest` passes — it must fail if `config.load()` is removed again (check by deleting the call, running `--selftest`, and restoring it).
- [x] 2.3 Replace the `process.openv` / `wait` / `close` runner with a single `os.execv(..., {try = true})` call that returns the exit code, and verify both the redirected enumeration calls and the build step keep working: `xmake test-report --suite=unit --unit-shards=2` is green, and a deliberately broken test source makes the run abort with the build-failure message before any binary runs (restore the source afterwards).
- [x] 2.4 Route the test binary's CRT assertion reports to stderr (`_CrtSetReportMode` / `_CrtSetReportFile`, Windows only) in `tests/test_main.cpp`'s `main`, so a `debug`-built child terminates on a hardening violation instead of blocking on the modal dialog, and verify `build/windows/x64/debug/tests.exe "[hardening]"` passes all three cases with no dialog and `xmake test-report --suite=unit` is green under `xmake f -m debug`.

## 3. `coverage` restores the caller's configuration

- [x] 3.1 Load the project configuration at the start of the task and replace the hardcoded `release` restore with the mode captured before switching (`--keep` still skips the restore), and verify `xmake f -m debug -y; xmake coverage --summary; xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'))"` prints `debug`, then restore `xmake f -m release -y`.
- [x] 3.2 Keep the explicit coverage-mode binary join (a child `xmake f` does not refresh this process's config or targets, so `targetfile()` cannot describe the coverage build), collapse the `find_tool` wrapper onto `assert`, and verify `xmake coverage --summary` still reports the totals row and that a failing run exits non-zero, names the failing step and restores the caller's mode.

## 4. `size` drops the dead PDB rebuild

- [x] 4.1 Delete the `pdb_stale` branch, its flag-injecting reconfigure and its restore block, and replace them with an explicit check that the symbol file exists (raising a message telling the caller to build with debug symbols); verify `xmake size -d` still prints the `.text`/`.rdata` per-object breakdown, and that renaming `build/windows/x64/release/encro.pdb` away makes the task print the new message instead of reconfiguring the project (rename it back).
- [x] 4.2 Verify the task leaves the build configuration untouched by running `xmake size -d` and then `xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'), config.get('cxxflags'), config.get('ldflags'))"` — the mode the caller had configured, `nil`, `nil`.

## 5. Integration and repository checks

- [x] 5.1 Run the full acceptance set on the release configuration: `xmake test-report --suite=all` green, `xmake run e2e_tests` green, `xmake build encro encro_e2e_tool tests e2e_tests` clean, and `xmake test-report --selftest` green.
- [x] 5.2 Update and confirm the repository documentation: `AGENTS.md`'s `--selftest` sentence names the configuration assertions it now makes, and its build/test/tooling bullets plus the `test-task` / `coverage-report` specs match the changed commands and behaviour, with no documented option removed.
- [x] 5.3 Confirm `CHANGELOG.md` needs no entry for this change, since `changelog-maintenance` excludes internal refactors and developer-tool fixes from the changelog.
- [x] 5.4 Record the two deferred risks in `docs/backlog.md` — `size --mode` reading a binary from a mode other than the configured one, and coverage's unexercised "no .profraw" branch — and verify both entries follow the file's Status/Found/Severity/Symptom/Root cause/Solution shape.

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the code-review stage the project requires, before committing.
- Archive the change once the review chain is satisfied.

## Review findings

Planning stage: one reviewer, Coherence and Ground-truth lenses. Code-diff stage: tier S (classifier chose S, 0.68 vs M 0.32), one reviewer carrying Standards, Spec and Leanness and running the coverage protocol itself. A fresh verifier re-checked the planning fixes; the code-stage fixes are re-verified below.

### Planning stage

- C1 `resolved` — design quoted `tests/**.cpp|tests/e2e/**`, an exclusion that matches nothing (85 files); it now quotes the shipped `tests/**.cpp|e2e/**` and states the root-relative rule.
- C2 / G1 `resolved` — the hardening requirement and the proposal line claimed a process-wide guarantee in every build mode while only the test binary routes CRT asserts; both are scoped to the test binary and the crash-record test.
- C3 `resolved` — design described the phony target as carrying "what `encro_core` publishes"; it now says it carries the list `e2e_tests` repeats, and the proposal's mention of the target was dropped when the target itself was removed (L1).
- C4 / G6 `resolved` — the test-task requirement demanded resolved `platform`/`mode` locals the design deletes, and its `--selftest` documentation did not mention the new assertions. The requirement now says the task locates binaries and report paths "through the loaded project configuration", a scenario requires the self-check to report the resolved mode, platform and binary path, and `AGENTS.md` names it.
- G2 `resolved` — "that configuration's build directory" read as a per-mode directory; it is now "the project build directory the caller selected", matching the mode-independent documented paths.
- G3 `resolved` — the restore requirement promised "the build configuration" while the closing `-c` discards everything but mode and platform; it now promises those two and says the other options are not preserved.
- G4 `resolved` — root `xmake.lua` is 356 lines (316 of them non-blank); the proposal's "316 lines" was the non-blank count. Corrected, with the plugin line estimates.
- G5 `resolved` — the two backlog entries were outside the proposal's Affected code and had no task; both are named now (Impact and task 5.4).

### Code-diff stage

- S1 `resolved` — `run()` returns the child's exit code, but `enumerate`'s two listing calls and the escaping probe discarded it, so a failing listing read as "this suite has no case"; the two listing calls and the probe now raise on a non-zero exit.
- S2 `resolved` — the `--selftest` menu description said "partitioning helpers on fixtures and exit"; it now names the resolved configuration too.
- S3 `rejected: pre-existing` — 15 Chinese comment lines remain in `plugins/coverage/xmake.lua` against `AGENTS.md`'s English-comment rule (16 before this change translated one of them). They predate this change and translating them is an unrelated chore; the diff translates only the comment line it already rewrites.
- L1 `resolved` — the new `encro_test_deps` phony target had a single dependent and copied `encro_core`'s list instead of sharing it; it is removed and `e2e_tests` keeps its own list (8 lines shorter than the target).
