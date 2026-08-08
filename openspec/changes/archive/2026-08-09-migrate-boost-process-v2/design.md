# Design — migrate boost.process v1 to v2

## Context

- Only three translation units use boost.process: `src/utils/utils.cpp` (production, `exec2Impl`), `tests/e2e/e2e_test_utils.cpp`, `tests/infra/crash_runtime_tests.cpp`.
- Boost 1.90 is installed; the unversioned `boost/process.hpp` now maps to v2; v1 requires explicit `boost/process/v1.hpp` (which all three files already do).
- v2 is an asio-based rewrite: `basic_process<Executor>` requires an executor, stdio are asio pipes, launching is driven by launcher/initializer objects with `on_setup`/`on_success`/`on_error` hooks.
- Verified v2 facts that shape this design (read from the installed 1.90 headers):
  - v2 **removed implicit command-line-string execution** (security-motivated); `v2::shell()` parses command lines explicitly.
  - v2's stdio pipe-binding overload (`process_io_binding(ReadablePipe&)`) **creates a fresh pipe per binding** — the v1 idiom of merging stdout+stderr by binding the same pipe object twice has no direct equivalent. A raw-`HANDLE` binding overload exists.
  - `windows::create_new_process_group` is a first-class initializer (replaces the e2e `detail::handler_base` hack).
  - `basic_process` destructor terminates an undetached running child — `RunningProcess::~RunningProcess`'s manual `valid() && running()` guard becomes unnecessary (`valid()` no longer exists).
  - Launch failure throws `system_error` from the default launcher (v1 threw `process_error`); no caller catches either — behavior preserved.
  - `environment::find_executable` replaces `search_path`; `start_dir` and `nullptr`-stdio (opens NUL//dev/null) are available.
  - POSIX default launcher is fork-based (`pdfork_launcher`) — same fork-in-multithreaded-host exposure v1 POSIX already had.

## Goals / Non-Goals

**Goals:**
- Replace every v1 reference with v2; zero v1 includes remain.
- Preserve the `subprocess-exec` spec contract exactly (exit codes, merged/separate stderr, line callbacks, stop-signal cancellation, detach fallback).
- Collapse the two `exec2Impl` `#ifdef` branches into one cross-platform implementation.
- Keep the test suite (unit + e2e) green as the migration's safety net.

**Non-Goals:**
- No boost version pin in `xmake.lua` (deliberate — see proposal).
- No behavioral improvements to exec2 beyond the spec'd contract (no new features, no API changes to `ExecResult`).
- No async API for exec2 (v2's async capabilities are not surfaced; exec2 stays synchronous).
- No POSIX launcher experimentation (default launcher; POSIX is the secondary platform).

## Decisions

### D1: Unified reader-thread design for exec2

One `asio::io_context` per call; the main thread polls `process.running()` every 20 ms and checks the stop signal; a single reader thread owns the output buffer and fires line callbacks; on exit the main thread joins the reader.

- v2 has **no non-blocking pipe peek** — the Windows `PeekNamedPipe` drain loop has no v2 equivalent, so the POSIX reader-thread pattern (already proven in this codebase) becomes the single implementation. All raw Win32 handle management (`PeekNamedPipe`/`CloseHandle`/`assign_sink`/`assign_source`) is deleted.
- Callbacks fire on the reader thread. This matches today's POSIX semantics; on Windows the affinity changes (see R4).
- Stop path (from the current POSIX branch, becomes universal): terminate → close read end to unblock the reader → wait 500 ms → detach child + detach reader thread if still stuck → return partial output with exit code 130.
- *Alternative considered:* full-async io_context design (`async_read` + `async_wait` + `steady_timer` for stop checks). Rejected: a much larger rewrite of the stop-signal integration for no user-visible gain; the reader-thread pattern is already battle-tested here.

### D2: Merged stderr via one pipe with a shared write handle

For `mergeStdErr`, create a single pipe with public asio API (`asio::readable_pipe` + `asio::writable_pipe` + `asio::connect_pipe`), bind the **same write handle** to both stdout and stderr via v2's raw-handle binding overload (`process_io_binding(HANDLE)`), and read from the readable pipe. This reproduces v1's OS-level interleaving exactly.

- *Alternative considered:* two pipes + two reader threads. Rejected: interleaving order becomes racy — an observable behavior change with no benefit.
- Applies identically on POSIX (fd-based bindings, same handle twice).

### D3: Explicit command-line parsing via `v2::shell()`

`exec2` receives full quoted command lines; parse them with `v2::shell()` to get exe + args. Quoting round-trip parity with v1 (which passed the raw command line to CreateProcess) is a risk — mitigated by a parity test (paths with spaces and quotes through the fake-toolchain e2e harness).

### D4: e2e helper substitutions

- `bp::ipstream` → `asio::readable_pipe` with a blocking read loop to EOF (two pipes, two reader threads — unchanged structure).
- `bp::null` stdin → `nullptr` binding; `bp::start_dir` → v2 `start_dir`; `bp::search_path` → `environment::find_executable`.
- `NewProcessGroup` detail hack → `windows::create_new_process_group` initializer.
- `RunningProcess` destructor drops the explicit `valid() && running()` terminate — v2's destructor terminates undetached children.

### D5: No version pin

`add_requires("boost[all]")` stays as-is; v2 migrates with the floating boost. The v1 detail hack (the reason a pin looked attractive) is gone after this change.

## Risks / Trade-offs

- **R1 — v2 API is still evolving** (a launcher/init refactor already happened between 1.88 and 1.90). → Migration targets 1.90's API; a future boost bump may need small mechanical fixes. The fragile v1-only coupling is gone; future breakage is in mainstream API, not `detail`.
- **R2 — asio compile-time weight** lands in `utils.cpp` and the test TUs. → Measure TU compile time before/after; acceptable since `boost[all]` is already on the include path and only a few TUs are affected.
- **R3 — asio pipe `close()` vs a pending blocking read** on the stop path (Windows): closing the read end must unblock the reader thread. → Verify at apply time; existing fallback (detach reader thread after join timeout) stays.
- **R4 — Windows callback thread affinity changes** to the reader thread. → Callers already tolerate reader-thread callbacks on POSIX; audit `onLine` callers (video/picture/pack progress parsing) and rely on the e2e stop/interruption tests.
- **R5 — `shell()` quoting round-trip** could differ from v1's raw command line for exotic quoting. → Parity test with spaces/quotes in tool paths (see D3).
- **R6 — shared write handle for stdout+stderr** depends on v2's handle-binding inheritance handling. → Verify with a merged-output test; fallback is two pipes with documented reordering (behavior change, last resort).

## Migration Plan

Order minimizes verification risk — test infrastructure first, production last, full suite after each step:

1. Migrate `tests/e2e/e2e_test_utils.cpp` + `tests/infra/crash_runtime_tests.cpp` (test-only; `xmake build tests` + `xmake run tests` + e2e stay green).
2. Migrate `src/utils/utils.cpp` (the unified exec2; the only production change). Full unit suite + e2e suite, including stop/interruption tests.
3. Sweep: no `boost/process/v1` references remain; remove now-unused includes.
4. Measure compile time of affected TUs; run `xmake format` + `xmake coverage` for regression confidence.

**Rollback:** revert the branch. v1 and v2 headers coexist in the installed boost, so each step is independently reversible.

## Open Questions

- Does `asio::readable_pipe::close()` reliably unblock a pending blocking `read_some` in another thread on Windows? (R3 — has a fallback, verified during apply.)
- Does v2's raw-handle binding keep the write handle inheritable when the same handle is bound to both stdout and stderr? (R6 — has a fallback, verified during apply.)

## Verification Results (apply phase)

- **R3 resolved**: `asio::readable_pipe::close()` unblocks a pending blocking read on Windows — proven by the `exec2 closes the output pipe after stop even if another process keeps stdout open` test (grandchild keeps the write end; close unblocks the reader; returns 130 + partial output).
- **R6 resolved**: binding the same raw write handle to stdout and stderr produces OS-level merged output — proven by the merged-stderr unit test and the video encode e2e suite (progress flows on stderr).
- **R5 resolved**: `v2::shell` → `exe()` + `args()` preserves the raw command line on Windows (raw `wchar_t*` passed to `CreateProcessW`); quoting round-trip verified end-to-end by the new e2e test with a toolchain installed under a spaced path.
- **R2 resolved**: compile time of `src/utils/utils.cpp` (release, `-O2 -flto=thin`, clang-cl 22): v1 baseline 7–8 s, v2 6–7 s — no asio compile-time regression.
- **R4 resolved**: all `onLine` sinks (`Store::markProgress`, progress bars, `vidState.mtx`) are mutex-guarded and already ran on the reader thread under v1 POSIX.
- **Coverage** (llvm-cov, unit + e2e suites): total 92.03% line coverage; `src/utils/utils.cpp` 79.86% lines / 92.31% regions. Uncovered lines in `exec2Impl` are only the defensive fallbacks (terminate-throw, detach-on-unkillable-child, stuck-reader) and the unused 4-arg overload — the same paths v1 left uncovered.
- **Build-infra fix**: `xmake.lua` coverage mode now passes `{force = true}` to the coverage cxxflags — xmake's `check.auto_ignore_flags` was silently dropping them for clang-cl, so `xmake coverage` produced no instrumentation before.
