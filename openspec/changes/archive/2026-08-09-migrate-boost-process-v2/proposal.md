# Migrate boost.process v1 to v2

## Why

boost.process v1 is frozen and the unversioned `boost/process.hpp` header already switched to v2 in Boost 1.90; v1 is now opt-in via `boost/process/v1.hpp`. The project's only v1-specific coupling — an e2e extension point that reaches into `bp::detail::handler_base` to create Windows process groups — is a compile-time landmine on every future Boost upgrade (the code comments acknowledge this by design). Migrating now removes the landmine, adopts the maintained API, and pays down the duplicated exec2 implementation in the process.

## What Changes

- **Replace all `boost/process/v1` usage with v2** in the three translation units that use it: `src/utils/utils.cpp` (the only production code), `tests/e2e/e2e_test_utils.cpp`, `tests/infra/crash_runtime_tests.cpp`.
- **Unify the two exec2 implementations** (Windows `PeekNamedPipe` poll loop + POSIX reader thread) into a single cross-platform reader-thread implementation, deleting the raw Win32 pipe-handle management (`PeekNamedPipe`/`CloseHandle`/`assign_sink`/`assign_source`). Observable behavior of `exec2` is preserved: exit code, merged/separate stderr, line callbacks, stop-signal termination, detach fallback, partial output on stop.
- **Explicit command-line parsing**: v2 removed implicit command-line-string execution; `exec2` callers pass full quoted command lines, so parsing goes through v2's `shell()` explicitly. Quoting round-trip semantics must match v1 on Windows.
- **Replace the `NewProcessGroup` `detail::handler_base` extension point** with v2's first-class `windows::create_new_process_group` initializer. **BREAKING** for the e2e helper internals only.
- **API substitutions**: `bp::ipstream` → `asio::readable_pipe`, `bp::null` → `nullptr` stdio binding, `bp::search_path` → `environment::find_executable`, `bp::start_dir` preserved via v2 `start_dir`.
- **No boost version pin** in `xmake.lua`: `add_requires("boost[all]")` stays floating (deliberate decision — the v1 detail hack was the reason a pin looked attractive; v2 removes it).
- Boost.asio becomes a compile-time dependency of the affected translation units (header-only, already provided by the existing `boost[all]` package).

## Capabilities

### New Capabilities

- `subprocess-exec`: pins the externally observable contract of `exec2`/`ExecResult` (exit codes, merged-stderr modes, line callbacks, stop-signal termination with timeout + detach fallback, partial-output guarantee) as the behavioral baseline this migration must preserve. No new behavior is introduced; the spec captures existing behavior so the rewrite cannot silently drift.

### Modified Capabilities

- None. No existing spec's requirements change; the new capability is the delta.

## Impact

- **Code**: `src/utils/utils.cpp` (exec2Impl, both `#ifdef` branches collapse into one), `tests/e2e/e2e_test_utils.cpp`, `tests/infra/crash_runtime_tests.cpp`.
- **Dependencies**: boost.process v2 + boost.asio (both header-only, inside existing `boost[all]`); no boost version pin and no other xmake.lua dependency change. One build-infra fix: the coverage-mode cxxflags now pass `{force = true}` (xmake's flag auto-check was silently dropping them for clang-cl, so `xmake coverage` produced no instrumentation — fixed during apply-phase verification).
- **Compile time**: increases for translation units including v2 (asio header weight); worth measuring.
- **Platform risk**: v2's POSIX default launcher is fork-based (`pdfork`); in a multithreaded host (thread-pool is in use) this needs a launcher decision — handled in design.md. Windows (primary platform) uses `windows::default_launcher` (CreateProcess), unaffected.
- **Verification surface**: existing `tests/utils_tests.cpp` plus e2e coverage (stop-signal, terminate, interruption, exit-code paths) is the safety net; the new `subprocess-exec` spec is the normative baseline.
