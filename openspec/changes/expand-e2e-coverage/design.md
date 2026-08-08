## Context

See proposal.md — Why. Current e2e infra (`tests/e2e/e2e_test_utils.{h,cpp}`) only supports synchronous process runs via boost::process v1 (`runProcess`/`runEncro`), which blocks until exit and never exposes the child. The fake media tool (`tests/e2e/fake_media_tool.cpp`) already supports env-controlled delay (`ENCRO_FAKE_FFMPEG_DELAY_MS`), output size, failure matching, and stderr injection, but the delay knob is unused by tests today. The product's stop-signal machinery (`src/infra/stop_signal.cpp`): `SetConsoleCtrlHandler` on Windows → sets a stop flag → 3-second force-exit watchdog → `ExitProcess(130)`; modules poll `stopsignal::isStopRequested()`.

Constraints:
- Windows primary (clang-cl); POSIX secondary (`src/utils/utils.cpp` has POSIX paths).
- boost 1.90, `boost/process/v1.hpp` only; e2e target must not add dependencies.
- Existing `[real-ffmpeg]` tests use `SKIP()` when tools are missing — same pattern for console-dependent tests.
- All e2e tests currently use `-j 1`; progress bars are disabled in non-TTY child anyway.

## Goals / Non-Goals

**Goals:**
- Async process handle with signal delivery, minimal API surface, zero product-code changes.
- Deterministic interruption timing via fake-tool delay + log polling (no sleeps-on-guess).
- Graceful `SKIP` when the platform can't deliver console events.

**Non-Goals:**
- Interactive stdin testing (typing answers to prompts) — out of scope, TTY plumbing is a separate problem.
- Real Ctrl+C sent to the *test runner* itself; only the encro child group receives events.
- Changing the fake tool's existing env-var contract (new vars are opt-in).
- Real-ffmpeg tests for NVENC/preset matrix (needs GPU/encoders); only h264 happy path + segment smoke.

## Decisions

### D1. Process handle: `RunningProcess` owning `bp::child` + stream readers

```
class RunningProcess {              // move-only
  bp::child child_;
  std::jthread stdoutReader_, stderrReader_;
  std::string stdout_, stderr_;     // guarded by mutex
public:
  auto wait(std::chrono::milliseconds timeout) -> ProcessResult;  // polls running()
  auto sendCtrlC() -> bool;                                       // platform-specific
  auto terminate() -> void;                                       // bp::child::terminate
  auto id() -> std::size_t;
  ~RunningProcess();               // terminate if not waited
};
```

`runEncroAsync(args, env, cwd)` mirrors `runEncro` but returns `RunningProcess`; the existing synchronous `runProcess` stays for the 18 unaffected tests. Rationale: the child's stdout/stderr pipes must be drained concurrently or the child blocks on a full pipe — the current `captureStreams` jthread pattern is reused as-is. Alternatives considered: boost::process v2 (`process` class, async) — rejected, would rewrite the whole utils file and drag in asio; raw `CreateProcessW` — rejected, reimplements pipes/env the boost v1 layer already handles.

### D2. Windows process-group isolation via a v1 handler initializer

`GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid)` requires the target to be a **process-group leader**; otherwise it fails (and with `pid=0` it would signal the test runner's own group — killing Catch2). boost v1 exposes `detail::handler_base` with `on_setup(exec)` — the same extension point `show_window.hpp` uses for `CREATE_NO_WINDOW` — so a ~10-line initializer sets `exec.creation_flags |= CREATE_NEW_PROCESS_GROUP` at child creation. This is the documented v1 extension pattern; it touches a detail member (`creation_flags`) the way boost's own handlers do. POSIX needs no group: `kill(pid, SIGINT)` targets one process. Alternatives considered: boost v2 `windows::create_new_process_group` (rejected, see D1); `CREATE_NEW_PROCESS_GROUP` via raw Win32 (rejected).

### D3. Signal delivery

- Windows: `GenerateConsoleCtrlEvent(CTRL_C_EVENT, child.id())` — delivered to the whole encro group, so the fake ffmpeg child also dies, which is exactly the real-user Ctrl+C behavior and exercises encro's abort-on-child-failure path.
- POSIX: `::kill(child.id(), SIGINT)`.
- Guard: `consoleCtrlEventsAvailable()` checks `GetConsoleWindow() != nullptr` (Windows) — with a console, caller and child share it; without one, `SKIP("...")` like `[real-ffmpeg]` tests do. POSIX: always available.

### D4. Deterministic interruption timing

Tests must not guess: start encro with `ENCRO_FAKE_FFMPEG_DELAY_MS=15000`, then poll the fake-tool log file until an `ffmpeg\t...` encode line appears (proves we're mid-encode), then signal. `wait()` timeout of ~10 s accommodates the 3 s force-exit watchdog. For the "cancel before any task started" case, signal immediately after startup, before any log line.

### D5. Fake tool opt-in additions

- `ENCRO_FAKE_FFPROBE_CHECK_INPUT=1`: ffprobe validates the probed path exists, else exit 2 — lets e2e cover the probe-failure path deterministically (default off; existing contract untouched).
- No new ffmpeg behavior needed: interruption relies on the fake tool having **no** Ctrl+C handler (default termination), which is already true.

### D6. Real-ffmpeg mp4 smoke (skip-guarded)

`testsrc2=duration=2:size=320x240:rate=10` + `sine=frequency=440:duration=2`, encode `-c:v libx264 -c:a aac` via real ffmpeg in the test fixture (same as `createRealSmokeVideo`), then run encro `-f mp4` and probe the result: `codec_name == h264`, audio stream present. Second smoke: 4-second source so segmentation triggers (>1 segment), run once, delete the concat output, resume, verify re-concat. Fixture duration: each real encode ~1–2 s.

## Risks / Trade-offs

- [boost v1 `creation_flags` is a detail member] → Mitigation: it is the identical pattern boost's own `show_window.hpp` uses; add a comment pinning the boost version range; e2e compile test will fail loudly on upgrade.
- [GenerateConsoleCtrlEvent fails without a shared console (CI detached mode)] → Mitigation: `consoleCtrlEventsAvailable()` probe + `SKIP`; the hard-kill (`terminate()`) interruption test still runs everywhere.
- [Ctrl+C timing race: signal lands between tasks, not mid-encode] → Mitigation: fake-tool log polling proves the encode is in flight before signaling; the assertion set is robust to either (exit 130 + state present + resume works).
- [Graceful-exit path may take up to 3 s (watchdog)] → Mitigation: `wait()` timeout ≥ 10 s; assert exit code 130, not wall-clock time.
- [Fake ffmpeg also receives Ctrl+C → its exit code is the CTRL_C status (0xC000013A), not 130] → Mitigation: tests assert encro's own exit code and state-file outcome, never the fake tool's reported exit code on the interrupted path.

## Migration Plan

Pure test additions — no deploy/rollback surface. Land as: utils infra (+ unit-ish e2e self-test via existing `[e2e]` tag) → interruption tests → remaining coverage tests. `xmake run e2e_tests` must stay green at each step; console-dependent tests self-skip on machines without a console.

## Open Questions

- Non-TTY prompt behavior (no `-y`, existing output): does the CLI fail fast, read EOF, or loop? The overwrite-prompt test asserts whatever the current behavior is — needs a 5-minute spike during implementation before writing its assertions. If it turns out to be a product bug, it gets its own change, not this one.
