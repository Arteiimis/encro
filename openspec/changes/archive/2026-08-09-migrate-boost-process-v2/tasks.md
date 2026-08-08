# Tasks — migrate boost.process v1 to v2

## 1. Test infrastructure migration (test-only, keeps harness green)

- [x] 1.1 Migrate `tests/e2e/e2e_test_utils.cpp` to v2: replace `boost/process/v1.hpp` with v2 headers, `bp::ipstream` → `asio::readable_pipe` (blocking read loop to EOF in `readProcessStream`), `bp::null` → `nullptr` stdio binding, `bp::start_dir` → v2 `start_dir`, `bp::search_path` → `environment::find_executable`
- [x] 1.2 Replace the `NewProcessGroup` `detail::handler_base` extension point with v2's `windows::create_new_process_group` initializer and delete the hack
- [x] 1.3 Update `RunningProcess::~RunningProcess` for v2: drop the `valid()` check (no longer exists); keep a guarded `running()` + `terminate()` before joining readers (v2's destructor terminates too late — after the readers block on pipe EOF)
- [x] 1.4 Migrate `tests/infra/crash_runtime_tests.cpp` to v2 (`ipstream` → `readable_pipe`)
- [x] 1.5 Build and run: `xmake build tests`, `xmake run tests`, `xmake build e2e_tests`, `xmake run e2e_tests` — all green (harness behavior unchanged)

## 2. Production migration: unified exec2

- [x] 2.1 Rewrite `exec2Impl` in `src/utils/utils.cpp` as a single cross-platform implementation: per-call `asio::io_context`, `basic_process` with `shell()`-parsed command, reader thread owning the output buffer and firing line callbacks, main thread polling `running()` + stop signal every 20 ms
- [x] 2.2 Merged mode (D2): single pipe via `asio::connect_pipe` + same write handle bound to stdout and stderr; separate mode: stderr → `nullptr` binding; delete all raw Win32 handle management (`PeekNamedPipe`, `CloseHandle`, `assign_sink`/`assign_source`)
- [x] 2.3 Preserve line-splitting semantics: split on `\n`, strip trailing `\r`, deliver partial trailing line only in the final result
- [x] 2.4 Preserve the stop path: terminate → close read end to unblock reader → 500 ms grace → detach child + detach stuck reader → return exit code 130 with partial output
- [x] 2.5 Verify launch failure still surfaces as an exception (v2 launcher throws `system_error`), matching the spec's missing-executable scenario
- [x] 2.6 Build and run full unit suite (`xmake run tests`) — `[utils]` stop-signal tests (terminate + pipe-close-after-stop) pass unchanged

## 3. Spec-conformance verification (subprocess-exec scenarios)

- [x] 3.1 Confirm every `subprocess-exec` spec scenario is covered by an existing or new test: exit codes (0/non-zero), missing executable, merged vs separate stderr, large output beyond one pipe buffer, callback-per-line, CRLF stripping, no trailing newline, stop-during-run, stop-pending, unresponsive child (detach fallback); add tests where gaps exist. Note: the unresponsive-child detach fallback is not automatable on Windows (TerminateProcess always kills) — it stays a defensive path covered by review, not by a test
- [x] 3.2 Add/verify merged-stderr interleaving test (R6 check: same write handle bound to both streams produces merged output)
- [x] 3.3 Add quoting parity test (R5): fake toolchain installed under a temp path containing spaces (and a quoted argument) — `shell()` round-trip must launch correctly
- [x] 3.4 Audit `onLine` callers (video/picture/pack progress parsing) for reader-thread callback tolerance (R4)
- [x] 3.5 Full e2e run (`xmake run e2e_tests`) including interruption, terminate, and stop-signal scenarios

## 4. Cleanup and quality gates

- [x] 4.1 Sweep: no `boost/process/v1` (or `bp::process::v1`) references remain anywhere in `src/` or `tests/`; remove now-unused includes
- [x] 4.2 `xmake format` on touched files; `xmake format -k check` clean
- [x] 4.3 Measure compile time of `src/utils/utils.cpp` and the migrated test TUs before/after (asio weight, R2) and record the delta in the change
- [x] 4.4 Run `xmake coverage` and confirm no coverage regression in the subprocess paths
