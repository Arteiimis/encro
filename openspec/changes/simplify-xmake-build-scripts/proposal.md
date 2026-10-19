# Proposal

## Why

The xmake side of this repo (root `xmake.lua`, 356 lines before this change, plus six plugin tasks, ~1,500 lines of Lua and ~590 lines of Python) re-implements several things xmake already ships — an exec wrapper, a tool lookup wrapper, a recursive `mkdir`, a core counter, a binary-path join, a hardcoded `xmake` subprocess call. While verifying which of those can be replaced by built-ins, two real defects surfaced that only a verification pass would have caught: neither the `test-report` nor the `coverage` task ever calls `config.load()`, so inside `on_run` both `config.get("mode")` and `config.plat()` return `nil`. `test-report` therefore always resolves the build mode to `release` (it cannot run in a `debug` or `coverage` configuration at all), and `coverage` restores a hardcoded `release` mode instead of the caller's. Separately, `size --breakdown` carries a PDB-rebuild branch that cannot fire in the Windows release build it analyses, and that would rewrite the user's `--cxxflags`/`--ldflags` if it ever did.

## What Changes

- **Configuration state**: `test-report` and `coverage` load the project configuration before reading mode, platform, build directory or target paths. `coverage` records the mode it found and restores that mode when it finishes (the `--keep` opt-out is unchanged).
- **Binary location**: `test-report`, which runs the configured mode's own binaries, locates each suite's binary through `project.target(<name>):targetfile()` instead of joining `builddir / plat / arch / mode / name..ext` by hand; its `os.arch()` and `.exe` locals go away. `coverage` and `size` keep their explicit mode-directory join, because each reads a binary built in a mode this process is not configured for.
- **`size`**: the PDB-missing-or-stale rebuild branch and the config rewrite that restores it are removed; the task reads the symbol file the release build already produces.
- **Debug-mode hardening record**: a test binary built for `debug` now routes CRT assertion reports to stderr instead of the modal dialog the MSVC debug STL opens before a hardening trap, so the `[hardening]` integration tests produce their crash record in every build mode (previously the dialog blocked the child, so the record never arrived and those cases failed outside `release`).
- **Plugin plumbing**: the runner that spawns one child at a time drops from a `process` object plus wait/close bookkeeping to `os.execv(..., {try = true})`; the `find_tool()` wrapping collapses onto `assert()`; every `"xmake"` subprocess call becomes `os.programfile()`; the recursive `makedirs` becomes `os.mkdir`; `cpu_count()` becomes `os.cpuinfo().ncpu or <default>`.
- **Root `xmake.lua`**: the nine `add_files` lines that enumerate the unit-test sources become one glob with the e2e directory excluded.
- **Explicitly unchanged** (each was a candidate, and each was rejected after verification, so a future reader does not retry it): the `coverage`/`size` runner that lets a tool's own output reach the terminal (xmake's `os.runv`/`os.vrunv` capture stdout and discard it on success, which would swallow the `llvm-cov` report and build progress), the hand-written `/MANIFEST:EMBED` link flags (xmake's `platform.windows.manifest` rule is gated on `has_tool("ld", "link")` and never fires with `lld-link`), the xpack block that collects the VC redistributable DLLs (its newest-first ordering cannot be expressed as a glob), the `batchcmds:rm` PDB trim (the install action copies the symbol file unconditionally), `read_content()` (`io.readfile` raises in the xmake sandbox on a missing file), `assert_instrumented()` and `shard_envs()` (the `check.auto_ignore_flags` policy is not a substitute for either, and `process.openv` replaces the child environment), `size`'s `--mode` option and its manual binary-path join (they select a binary built in a mode other than the configured one), and the `tidy`/`include-cleaner`/`fmt` tasks that have no built-in equivalent without losing caching or SARIF output.

## Capabilities

### New Capabilities

None. This change removes duplication and fixes configuration handling; it does not introduce new user-facing capability.

### Modified Capabilities

- `test-task`: adds a requirement that the task resolves the build mode, platform and each suite's binary through the loaded project configuration, so a run follows the configuration the caller selected instead of assuming `release`.
- `coverage-report`: adds a requirement that the command restores the caller's build configuration when it finishes (unless `--keep` is given), instead of a fixed mode.
- `stdlib-hardening`: adds a requirement that a hardening violation terminates the test process in a `debug` configuration too, without waiting on a CRT assertion dialog, so the crash-record integration test no longer depends on which mode the suite was built in.

## Impact

Affected code: `xmake.lua`, `plugins/test_report/xmake.lua`, `plugins/coverage/xmake.lua`, `plugins/size/xmake.lua`, `tests/test_main.cpp`, and the two deferred risks recorded in `docs/backlog.md`. No change to `src/`, to the Python scanners, to the `fmt`/`tidy`/`include-cleaner` tasks, to the `xpack` packaging block's behaviour, or to any report path, suite selection, shard default or filter semantic. The xmake-side changes are observable only in non-`release` configurations and in the mode left behind after a coverage run; the `tests/test_main.cpp` change is observable only in a `debug` build, where a hardening violation used to open a dialog.
