# Tasks

## 1. Root build script

- [ ] 1.1 Replace the nine per-directory `add_files` lines of the `tests` target with `add_files("tests/**.cpp|e2e/**")`, and verify with `xmake lua -c "import('core.project.config'); import('core.project.project'); config.load(); local t = project.target('tests'); local n = 0; for _, f in ipairs(t:sourcefiles()) do n = n + 1 end; print(n)"` that the target still reports 80 source files (the same set the per-directory lines produced).
- [ ] 1.2 Evaluate a `phony` dependency target for the package/syslink/include list `e2e_tests` repeats: its public propagation works (`xmake show -t e2e_tests` attributed `-> dep(...) -> packages` to it and `xmake build e2e_tests` linked), but with one dependent it holds a copy of `encro_core`'s list rather than a share and `e2e_tests` keeps its own list.

## 2. `test-report` follows the configured build

- [ ] 2.1 Load the project configuration at the start of the task (`config.load()`), replace the `platform`/`arch`/`mode`/`ext`/`bin_dir` locals with `path.absolute(project.target(<name>):targetfile())` per suite, and verify `xmake f -m debug -y; xmake test-report --unit-shards=1` builds and runs the debug `tests.exe` (the run's own reported binary path and the `--selftest` line name the debug directory) with the report still landing at the documented `build/last-test-report.xml`, then restore `xmake f -m release -y`.
- [ ] 2.2 Add the `--selftest` checks for the loaded configuration reporting a mode and a platform and for the resolved binary path being absolute, and verify `xmake test-report --selftest` passes — it must fail if `config.load()` is removed again (check by deleting the call, running `--selftest`, and restoring it).
- [ ] 2.3 Replace the `process.openv` / `wait` / `close` runner with a single `os.execv(..., {try = true})` call that returns the exit code, and verify both the redirected enumeration calls and the build step keep working: `xmake test-report --suite=unit --unit-shards=2` is green, and a deliberately broken test source makes the run abort with the build-failure message before any binary runs (restore the source afterwards).
- [ ] 2.4 Route the test binary's CRT assertion reports to stderr (`_CrtSetReportMode` / `_CrtSetReportFile`, Windows only) in `tests/test_main.cpp`'s `main`, so a `debug`-built child terminates on a hardening violation instead of blocking on the modal dialog, and verify `build/windows/x64/debug/tests.exe "[hardening]"` passes all three cases with no dialog and `xmake test-report --suite=unit` is green under `xmake f -m debug`.

## 3. `coverage` restores the caller's configuration

- [ ] 3.1 Load the project configuration at the start of the task and replace the hardcoded `release` restore with the mode captured before switching (`--keep` still skips the restore), and verify `xmake f -m debug -y; xmake coverage --summary; xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'))"` prints `debug`, then restore `xmake f -m release -y`.
- [ ] 3.2 Keep the explicit coverage-mode binary join (a child `xmake f` does not refresh this process's config or targets, so `targetfile()` cannot describe the coverage build), collapse the `find_tool` wrapper onto `assert`, and verify `xmake coverage --summary` still reports the totals row and that a failing run exits non-zero, names the failing step and restores the caller's mode.

## 4. `size` drops the dead PDB rebuild

- [ ] 4.1 Delete the `pdb_stale` branch, its flag-injecting reconfigure and its restore block, and replace them with an explicit check that the symbol file exists (raising a message telling the caller to build with debug symbols); verify `xmake size -d` still prints the `.text`/`.rdata` per-object breakdown, and that renaming `build/windows/x64/release/encro.pdb` away makes the task print the new message instead of reconfiguring the project (rename it back).
- [ ] 4.2 Verify the task leaves the build configuration untouched by running `xmake size -d` and then `xmake lua -c "import('core.project.config'); config.load(); print(config.get('mode'), config.get('cxxflags'), config.get('ldflags'))"` — the mode the caller had configured, `nil`, `nil`.

## 5. Integration and repository checks

- [ ] 5.1 Run the full acceptance set on the release configuration: `xmake test-report --suite=all` green, `xmake run e2e_tests` green, `xmake build encro encro_e2e_tool tests e2e_tests` clean, and `xmake test-report --selftest` green.
- [ ] 5.2 Update and confirm the repository documentation: `AGENTS.md`'s `--selftest` sentence names the configuration assertions it now makes, and its build/test/tooling bullets plus the `test-task` / `coverage-report` specs match the changed commands and behaviour, with no documented option removed.
- [ ] 5.3 Confirm `CHANGELOG.md` needs no entry for this change, since `changelog-maintenance` excludes internal refactors and developer-tool fixes from the changelog.
- [ ] 5.4 Record the two deferred risks in `docs/backlog.md` — `size --mode` reading a binary from a mode other than the configured one, and coverage's unexercised "no .profraw" branch — and verify both entries follow the file's Status/Found/Severity/Symptom/Root cause/Solution shape.

## Workflow follow-up

- Run `openspec-verify-change` for this change, then the code-review stage the project requires, before committing.
- Archive the change once the review chain is satisfied.
