# Design

## Context

See proposal.md — Why. The constraints that shape the approach, all measured against the installed xmake (3.1.2+HEAD.5d21e9daf) rather than assumed:

- A plugin task's `on_run` starts with `core.project.config` **unloaded**: `config.get("mode")` and `config.plat()` return `nil` there, and only `config.builddir()` answers from defaults. Verified in a scratch project with a plugin task after `xmake f -m debug` (`mode: nil, plat: nil`); `size` already calls `config.load()` and is the only task that reads the right mode.
- `io.readfile` raises inside the xmake sandbox on a missing file (`core/sandbox/modules/io.lua`, `sandbox_io.readfile`), so the report task's `read_content` guard is required — the "missing file is a verdict input" path must not raise.
- `os.runv` / `os.vrunv` capture the child's stdout and stderr into a temp file and surface them only when the child fails (`core/base/os.lua`, `os.runv`); the successful child's output is discarded. They are quiet runners, not substitutes for a runner whose output the user needs.
- `os.execv` without `try` raises and hides the exit code; with `{try = true}` it returns the code (probe: `3`) and `nil` only when the process cannot be spawned, and it still lets the child inherit the terminal unless `stdout`/`stderr` is given.
- `process.openv` replaces the child's environment with the list it is given (probe: a parent-only variable is absent in the child), which is why the shard runner carries the whole environment and cannot be swapped for `os.execv`.
- `target:has_tool("ld", "link")` is `false` for this project (`ld` is `lld-link`), which is why xmake's `platform.windows.manifest` rule never fires here.
- `os.mkdir` creates intermediate directories and raises on failure in the sandbox; `project.target(name):targetfile()` returns the configured mode's path but **relative** to the project directory; `os.cpuinfo().ncpu` and `os.default_njob()` differ (16 vs 18 on this host — the latter is a parallelism, `ncpu + 2`).
- A **child** `xmake f -m …` leaves this process's view stale: `config.get("mode")` still reports the old mode and `targetfile()` still resolves the old mode's path (probe), and the modules expose no `unload` to force a reload.
- The MSVC STL's `_STL_REPORT_ERROR` runs `_RPTF0(_CRT_ASSERT, …)` **before** the trap (`yvals.h:252-254`). In a `_DEBUG` build that report opens a modal dialog and blocks the process, so a deliberate hardening violation in a child neither terminates nor prints its crash record; in a release build the same macro expands to nothing and only the trap runs. Measured: a `debug` run of the suite failed two of the three `[hardening]` cases with the dialog's `vector subscript out of range` (report `failures="8"`), and all three pass after the test binary routes CRT asserts to stderr.

## Goals / Non-Goals

**Goals:**

- Every plugin reads the configuration it acts on, so a run follows the caller's configuration.
- Each hand-rolled helper is either replaced by a built-in with the same behaviour or kept with a comment saying why no built-in fits.
- Net line reduction with no change to report paths, suite selection, shard defaults, filters, or the shard-level failure semantics.

**Non-Goals:**

- No change to the Python scanners, to the `xpack` block, to the Windows manifest flags, or to `test-report`'s enumeration/partition/cost-model logic.
- No attempt to port the xmake scripts onto the built-in `xmake test`, `xmake format`, `xmake check clang.tidy` or the `format-plugin` addon — see proposal.md for why each loses behaviour the repo relies on.
- No change to `size`'s `--mode` option or its manual binary-path join: it deliberately reads a binary built in a mode other than the configured one.

## Decisions

**Configuration is loaded at the top of `on_run`, before any config read.** `test_report` and `coverage` gain `config.load()` as their first statement (after the option/menu setup). Alternative considered: reading the mode from `.xmake/**/xmake.conf` directly — rejected, it duplicates xmake's own resolution and breaks on a custom build directory/policy. Consequence to keep in mind: once loaded, `config.plat()` returns the real platform instead of the host, so `coverage` passes the configured platform to its `xmake f -p …` calls, which is what it always meant to do.

**`coverage` captures the previous mode before reconfiguring and restores exactly that.** It reads `config.get("mode")` after `config.load()`, keeps it in a local, and passes it to the closing `xmake f -m <previous>` — the restore stays inside the same `try`/`catch` finish path, so a failing run still restores. The closing reconfigure keeps its `-c`, so like the reconfigure that starts the run it drops options other than mode and platform; the requirement promises those two and says so. `--keep` skips it as today. Alternative considered: remember nothing and restore `release` — rejected, already the bug.

**Binaries are located through the target, and the path is made absolute.** `test_report` replaces the `builddir / plat / arch / mode / name..ext` join with `path.absolute(project.target(name):targetfile())` and drops the `platform`/`arch`/`mode`/`ext`/`bin_dir` locals that fed it. A target that does not exist aborts with a message naming it. Alternative considered: keep the join — rejected, it encodes assumptions (`<name>.exe`, the directory layout) that the target already knows, and it is what made the missing `config.load()` invisible.

**`coverage` keeps its explicit coverage-mode path join, and gains `config.load()` only for the platform and the caller's mode.** It runs the binaries of a mode it switches to with a child `xmake f`, and that child does not refresh what this process has loaded: a probe re-read `mode=debug` and `targetfile()=build/windows/x64/debug/tests.exe` after a child reconfigure to `coverage`, and neither `config.unload` nor `project.unload` exists to force a reload. So `targetfile()` cannot describe a binary the process has not configured, and the join stays (as it does in `size`, which likewise reads a mode other than the configured one).

**`size` loses its PDB rebuild branch and gains a clear failure instead.** The branch cannot fire in the configuration it analyses (a Windows release build already writes the symbol file next to the executable, mtimes equal — verified by running `xmake size --breakdown`), and if it ever did fire it would rewrite `--cxxflags`/`--ldflags`, wiping whatever the user had configured. It is replaced by an explicit check that the symbol file exists, raising a message that says to build with debug symbols. Alternative considered: keep the branch as a portability guard — rejected, `llvm-pdbutil` is Windows-only anyway, so there is no platform on which it could usefully run.

**Single-child spawning uses `os.execv` with `{try = true}`; only the concurrent shard spawn keeps `process`.** In `test_report`, the `process.openv` + `wait` + `close` runner becomes one `os.execv` call that returns the exit code: the build step keeps the child's output on the terminal and the enumeration steps keep their `stdout` redirect. The shard spawn keeps `process.openv`, because it needs `envs` + per-shard `stdout` + concurrency in one call and because only that path is started all at once and reaped in a later pass. `coverage` and `size` keep their existing runner: their children's output *is* the result (`llvm-cov report`, `llvm-size -A`), and the quiet runners would discard it.

**The regression test for the mode bug lives in the task's `--selftest`.** A check there asserts that the loaded configuration reports a mode and a platform, and that a suite's resolved binary path is absolute — the two facts whose absence let the task look for a `release` binary under any configuration. It costs three assertions inside a harness the task already owns and runs without building anything; an end-to-end run under `debug` stays the acceptance check, not the regression gate.

**The test binary routes CRT assertion reports to stderr once, in `main`.** `_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE)` + `_CrtSetReportFile(…, _CRTDBG_FILE_STDERR)` make the debug STL's pre-trap report go to the stream the parent already captures, so the trap is the whole record in every mode. Alternative considered: set it inside each crash child — rejected, the in-session case triggers the violation from inside a normal Catch2 session, and a future probe would have to remember it. Alternative considered: skip the `[hardening]` cases when the debug iterator level is non-zero — rejected, that deletes the coverage the spec asks for precisely in the configuration that changes the code path.

**Mechanical replacements.** `os.programfile()` replaces the literal `"xmake"` in every subprocess call; `assert(find_tool(x), msg)` replaces the two `find_tool()` wrappers (`fmt` and `tidy` already read that way); `os.mkdir` replaces the recursive `makedirs`; `os.cpuinfo().ncpu or 4` replaces `cpu_count` (not `os.default_njob()`, which is a parallelism).

**Root build script: one glob for the unit-test sources.** `add_files("tests/**.cpp|e2e/**")` replaces the nine per-directory lines (verified set-identical: 80 files, both ways) — the exclusion is written relative to the glob root (`tests/`), because that is what `os.match` matches an exclusion against: `tests/**.cpp|tests/e2e/**` matches all 85 files and excludes nothing. Alternative considered and dropped in code review: a `phony` target carrying the public packages/syslinks/includedirs `e2e_tests` repeats from `encro_core`. Its propagation works (a dependent compiled against its public include, saw its define, linked its syslink), but `e2e_tests` is its only dependent, so it holds a *copy* of `encro_core`'s list rather than sharing it — and inlining the list back measured 8 lines shorter. Alternative considered: have `e2e_tests` depend on `encro_core` — rejected, it would link the product library into the e2e binary, which is exactly what the e2e suite must not do.

## Risks / Trade-offs

- [`targetfile()` returns a project-relative path] → always wrap it in `path.absolute()` before spawning; a relative path passed to a child that runs in another working directory would break.
- [`config.load()` makes previously `nil` values real, so `test_report` now honours a non-`release` configuration — a path no test has exercised] → exercise one shard in `debug` during verification, and keep the existing release-mode flow as the acceptance run.
- [`os.execv` inherits the terminal, so a build failure's output arrives before the raised message rather than inside it] → keep the raised message naming the step and its exit code; do not capture the build output.
- [Deleting the `size` PDB branch removes the only path that produced symbols on demand] → the failure message names the fix (`build with debug symbols`), and the release build always writes them.

## Migration Plan

Single commit per functional area is not useful here; the change is one commit for the scripts plus its `tasks.md` checkboxes. Rollback is `git revert` of that commit — no data, config or artifact format changes, and no release-facing behaviour moves.
