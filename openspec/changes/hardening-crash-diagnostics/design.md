## Context

`crash::installHandlers()` (src/infra/crash_runtime.cpp) already installs a Windows unhandled-
exception filter, a terminate handler, and Linux signal handlers, with a 3-tier durable write
chain (direct log append → async logger → stderr) and an NDJSON companion record. Both `encro`
and `tests` call it from `main`. A self-test mode `--encro-crash-child` raises a custom
non-continuable exception and is exercised by an integration test via `boost::dll::
program_location()`.

Two facts found while exploring (both verified against the local toolchain: clang-cl 23.1 +
MSVC STL v14.51, Catch2 v3.15.2):

- Catch2 v3.15.2's `FatalConditionHandler` (compiled into its static lib) calls
  `SetUnhandledExceptionFilter` when the test session runs, replacing our filter — a crash
  inside a test never reaches `crash::` machinery today.
- Under clang-cl, an MSVC STL hardening failure invokes the doom function
  `__builtin_verbose_trap` → `ud2` → exception 0xC000001D — a normal first-chance exception,
  interceptable; under MSVC `cl` it would be `__fastfail` (bypasses all user-mode handlers),
  but this project builds with clang-cl only.

Linux builds use clang + libstdc++ with no hardening define today; the project has decided
against a libc++ migration.

## Goals / Non-Goals

**Goals:**
- Crash records survive crashes inside Catch2 test runs (the primary diagnosis gap).
- Crash records identify the running test so parallel-shard logs locate the failure.
- Crash stacks resolve to symbols on Windows release builds.
- libstdc++ precondition checks on Linux, verified end-to-end in CI (Linux-only CI).
- Windows VEH path verified locally; no Windows CI job (per decision).

**Non-Goals:**
- Windows CI job / runner setup.
- libc++ (`_LIBCPP_HARDENING_MODE`) support — no migration planned.
- `_MSVC_STL_DOOM_FUNCTION` customization — not needed while clang-cl's `ud2` path is
  interceptable (see Decisions).
- `_GLIBCXX_DEBUG` (ABI-breaking) or any hardening for prebuilt third-party binaries.
- Catching `abort()`/`__fastfail` (0xC0000409) — bypasses all user-mode handlers by Windows
  design; out of reach for any user-space fix.

## Decisions

### D1: First-chance VEH handler, not filter replacement or Catch2 config

`AddVectoredExceptionHandler(CallFirst=1, handler)` in `installHandlers()`; on a fatal code set
{0xC0000005 AV, 0xC000001D illegal instruction, 0xC00000FD stack overflow} it writes the
standard crash report and returns `EXCEPTION_CONTINUE_SEARCH`; all other codes (including C++
exceptions 0xE06D7363 and Catch2's test-failure exceptions) return immediately.

- Why not re-install our filter after Catch2 engages: `SetUnhandledExceptionFilter` has a
  single slot and no chaining API; ordering battles with `FatalConditionHandler`'s own
  save/restore are fragile.
- Why not `CATCH_CONFIG_NO_WINDOWS_SEH`: `FatalConditionHandler`'s implementation is compiled
  into Catch2's static lib and engaged from `Session::runInternal` inside that lib — a
  define on our translation units cannot change it.
- VEH runs before everything, cannot be displaced, and a switch on the exception code makes
  the non-fatal path a single comparison — negligible overhead for normal test-failure
  exceptions.
- After our report, `CONTINUE_SEARCH` keeps Catch2's message + termination flow intact (spec:
  interception must not change termination behavior).
- When the same exception later reaches our own UE filter (paths without Catch2, e.g. `encro`
  itself or the crash-child), a saved `ExceptionRecord` pointer match suppresses the duplicate
  report — production crashes keep exactly one record (single atomic compare).

### D2: No doom-function customization for hardening failures

Under clang-cl, hardening failures trap via `ud2` (0xC000001D) which D1 intercepts — a custom
`_MSVC_STL_DOOM_FUNCTION` (forced-include header + command-line macro, declaration-order
fragile, applies to every TU) would buy nothing. Revisit only if the project ever builds with
MSVC `cl`, where `__fastfail` is unreachable from user space (would need doom → our reporter).

### D3: Context provider as an injected `std::function`, not a Catch2 dependency

`crash::setCrashContextProvider(std::function<std::string()>)` lives in crash_runtime (which
also builds into `encro`, where Catch2 doesn't exist). test_main installs a lambda calling
`Catch::getResultCapture().getCurrentTestName()` (available in v3.15.2's
`interfaces/catch_interfaces_capture.hpp`). The provider is read inside the report path; a
throwing/hanging provider is guarded by try/catch with a silent fall-through (crash path must
never die inside itself). Allocation during non-stack-overflow crashes is safe; stack-overflow
reports may omit context (documented trade-off, see Risks).

### D4: One `--encro-crash-child=oob` mode serving both platforms

The child triggers `vector::operator[]` out-of-range after installing handlers. On Windows this
traps via hardening (0xC000001D → VEH report); on Linux it trips a libstdc++ assertion →
printed reason → `abort()` → SIGABRT → existing signal handler report. The integration test
asserts platform-neutral properties (non-zero exit, `[CRASH]`, stacktrace present) plus
platform-specific extras under `#if defined(_WIN32)` (0xC000001D in the reason). One test
verifies the Linux chain in CI and the Windows chain locally — no platform-skip tags needed.
The child also installs a context provider returning a fixed name, verifying D3's plumbing
end-to-end.

### D5: PDBs via `set_symbols("debug")` under release, Windows only

Without a PDB, dbghelp-backed stack traces print raw `module+offset` (verified). Enable debug
symbols for Windows release builds so `std::stacktrace`/boost resolve `module!function` and
source lines. Symbols don't alter codegen; xpack ships the exe only, so release artifacts are
unchanged. Linux release is left as-is to avoid CI binary-size/build-time growth (its crash
stacks already resolve through the existing pipeline); revisit if Linux symbols become a
diagnosis pain.

### D6: libstdc++ assertions define at the global xmake level

`add_defines("_GLIBCXX_ASSERTIONS=1")` in the non-Windows block next to the existing Windows
hardening defines (mirrors the platform symmetry). Command-line defines reach every TU before
any header. Header-inline checks affect only our TUs; prebuilt/system libraries are untouched
(no linkage or layout change — unlike `_GLIBCXX_DEBUG`).

## Risks / Trade-offs

- [Stack-overflow (0xC00000FD) report may truncate: the crashing thread's stack is nearly
  exhausted while the report allocates] → Catch2's `SetThreadStackGuarantee` reserves some
  stack during test runs; accept a possibly partial report as the ceiling and document it.
  `ponytail:` ceiling — per-thread guard-page-aware reporter only if SOF diagnosis ever
  matters in practice.
- [VEH runs on every first-chance exception] → Cost is one integer comparison for non-fatal
  codes; no measurable impact expected.
- [`_GLIBCXX_ASSERTIONS` may surface latent out-of-bounds bugs on Linux CI] → That is the
  point; failures now come with a crash record + reason instead of silent corruption. Fix bugs
  as they surface; the checks are cheap and ABI-safe.
- [Double report risk is structural, not child-only: on paths without Catch2 (`encro` itself,
  crash-child) a fatal-code crash would hit both the VEH report and our UE filter] → UE filter
  skips when the `ExceptionRecord` pointer matches the one VEH already reported (see D1);
  inside real test runs Catch2's filter terminates first, so each path yields exactly one
  record.
- [Release PDBs grow build directory size on Windows] → Symbols don't change shipped binaries;
  PDBs stay in build/. Acceptable for diagnosis value.

## Migration Plan

No data or interface migration. Rollback = revert the single commit (define removal restores
prior behavior; VEH registration is additive and inert for non-fatal codes).

## Open Questions

None — Windows-CI deferral and Linux assertions inclusion were decided with the user before
planning.
