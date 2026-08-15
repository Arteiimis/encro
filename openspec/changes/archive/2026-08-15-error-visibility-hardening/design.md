# error-visibility-hardening — Design

## Context

The audit behind this change (see proposal.md — Why) identified that diagnosability failures are rooted in error handling, not log coverage: `flushSnapshot` is `void`, scanner ignores `std::error_code`, pack exceptions are converted to default-constructed success results, and crash-tier-2 relies on an async post that `std::_Exit` never drains. The specs (specs/error-visibility/spec.md) pin the required observable behavior. This design covers the how.

## Goals / Non-Goals

**Goals**
- Every fix is a small, local change at the error source — no new error-handling architecture.
- All new log records reuse the existing `DEFINE_LOGGER`/`LOG_*` machinery and module tags already registered.
- Error *message text* (user-visible and test-asserted) stays unchanged; we only add records where there are none.

**Non-Goals**
- Structured error codes in `eh::Result` (78 signatures / 101 call sites / 25+ test assertions pinning text) — deliberately deferred; recorded as backlog in proposal.md.
- Changing the async logging architecture (queue size, block policy, sink layout).
- Rewording existing error messages or changing the `logging-behavior` / `subprocess-exec` / `job-state-resume-matching` / `video-frame-resume` contracts.

## Decisions

### D1: Job-state persistence failures propagate via `eh::Result` (mutators log internally)

`detail::flushSnapshot` and `Store::flushLocked` change from `void` to `eh::Result<void>`; `initialize` propagates so a failed initial flush fails the run at the top-level error path. The best-effort mutators (`mark*` / `setStage` / `requestCancel` / `flush`) stay `void`; a private `persistLocked(operation, force)` helper runs every save and `LOG_ERROR`s the failure with the operation name, so a failed save is always visible and call sites need no per-call handling.

- *Why*: Every mutator call site (27 across video/picture/pack) has no recovery action — the encode/pack outcome is already decided, and a propagation channel would be discarded anyway (all callers sit in `void(Store&)` lambdas or tail positions). Returning `eh::Result<void>` everywhere produced only `[[nodiscard]]` warnings and `(void)` noise; the truthful contract is *best-effort, failures logged*. `initialize` keeps its result because it has a real consumer (pipeline → top-level error path).
- *Alternatives considered*: (a) every call site checks and logs — duplicate log records (the store already logged) and noise at 27 sites; (b) swallow-and-continue (status quo) — the bug. Rejected. (c) `eh::Result<void>` on all mutators — signature-only propagation, warnings, no consumer. Rejected after implementation review.
- *Hot-path note*: `markProgress` may run on the 20ms monitor loop; failures are throttled by the existing `lastFlushAtMs` gate and log once per save attempt.

### D2: Pack run failure = any task result failure

`pack_service.cpp:252-256` currently checks only `packResults[index]` (default-constructed = success) and ignores `runRes.results`. Fix: a group is failed when **either** its `packResults` entry is failed **or** any `runRes.results[i]` is failed; on the latter, the exception/error message from `runRes.results[i]` becomes the group error and is logged at the existing error paths.

- *Why*: `runRes.results` already carries the per-task error strings (task_executor.cpp:91-99); the bug is purely a dropped check. No executor changes needed.
- *Alternative*: making the executor rethrow — rejected; the executor's capture-and-report contract is correct, the consumer was wrong.

### D3: Scanner reports errors instead of swallowing `ec`

`media_scanner` returns `eh::Result<std::vector<fs::path>>` (or equivalent with a warning list): the input-root `is_directory` check propagates `ec` as an error; mid-walk iteration failures are collected and surfaced as warnings (logged by the caller), not silently truncated. Both callers (`video_process`, `picture_process`) then distinguish: unreadable root → error + non-zero exit; empty result → current "no matching files" behavior.

- *Why*: root-cause fix in the shared function — both callers route through it (per the audit, video currently exits 0 on unreadable roots, picture misattributes the cause).
- *Exit-code note*: this intentionally changes video's false-success exit 0 → non-zero for unreadable roots (declared in proposal.md — Impact).

### D4: Task exceptions logged at the executor

`task_executor.cpp` (already has `DEFINE_LOGGER(logtags::CORE_TASK)`, currently zero calls) logs the exception message at error level in the existing catch block, and the message already flows into `results[i]`. Video callers keep checking `has_value()`; additionally `video_batch_execution` stores the actual message into `state.lastError` (instead of `"encoding failed"`) when the result carries one.

- *Why*: one log site fixes both pack and video consumers; the executor is the natural chokepoint.
- *Why not log at callers*: pack already benefits from D2's message propagation; video would need per-caller logging — more sites for the same coverage.

### D5: Cancellation evidence + watchdog direct write

`stop_signal.cpp` logs an info record when a stop request is registered (Windows `SetConsoleCtrlHandler` runs on a dedicated thread — safe for async logging; POSIX handler stays signal-safe by logging from the polling/watchdog path instead of inside the handler). For the 3-second force-exit watchdog, `crash_runtime` exposes `crash::writeDirectLogLine(std::string_view)` (the existing tier-1 ofstream-append logic, made public) and `stop_signal` calls it immediately before `ExitProcess(130)`.

- *Why*: the watchdog path bypasses everything (no crash handler, no queue drain) — only a direct synchronous append can guarantee the record.
- *Alternative*: calling `logging::shutdown()` before `ExitProcess` — rejected: shutdown can block on queue drain (block policy) precisely when the watchdog is already giving up.

### D6: Crash-report durability — retry the direct write, keep tiers

`tryWriteDirectToLogFile` gains a short bounded retry (e.g. 3 attempts, ~10ms backoff) to cover the µs-scale rotate close→reopen window where the file handle is momentarily unavailable. Tier-2 (async logger) remains as a best-effort middle tier with its known `_Exit` limitation documented in code; tier-3 stderr remains the final guarantee. The direct-write line format gains milliseconds + `%z` to match `kLogPattern`.

- *Why retry instead of synchronizing tier-2*: an `async_logger` cannot flush synchronously by design; the blind window is µs-scale and the direct path is already 99.99% reliable — a tiny retry closes it without redesigning the fallback chain.
- *Why format matters*: crash lines land next to regular lines from the same second; millisecond + offset precision makes the sequence sortable (spec: "Direct write timestamps").

### D7: Periodic flush via `spdlog::flush_every`

`setup()` registers `spdlog::flush_every(std::chrono::seconds(1))`. `registry::shutdown()` resets the periodic flusher, so existing setup/shutdown test pairing is unaffected. Bounds hard-kill loss to ≤1s of buffered non-error lines (in practice ≤512B MSVC stdio tail today, plus whatever accumulated in the interval).

- *Why 1s*: negligible write overhead (rotating file sink), meaningfully smaller loss window than "only on error or buffer-full".
- *Alternative*: per-iteration `flush()` in the encode loop — rejected, more code and couples logging policy to video internals.

### D8: WebP retry tiers update the forensic snapshot

The WebP adaptive loop re-writes `state.subprocessCmdline` before each quality-tier attempt (currently written once with the initial q=80 command at video_encode_runner.cpp:382/647), so the error-attached snapshot shows the failing tier's command.

- *Why*: the snapshot claims to describe "the command in flight"; a stale command actively misleads diagnosis (spec: "Forensic snapshot reflects the in-flight command").

## Risks / Trade-offs

- [`mark*` signature change touches 13 call sites across video/picture/pack] → Mitigation: compile-time enforcement; each call site change is a mechanical `if (!res) LOG_ERROR`; error text unchanged so existing tests stay green.
- [Scanner interface change affects two callers (video + picture)] → Mitigation: D3 lands the Result-returning shape once; both callers updated in the same task; behavior of "no matching files" path unchanged.
- [Exit code 0 → non-zero for unreadable roots may surprise scripted users] → Mitigation: declared in proposal; error message names the root so the cause is visible; warning-level console line added.
- [Watchdog direct write races the async sink on the same file] → Mitigation: same bounded window as crash tier-1 (µs-scale, append-only); accepted, consistent with D6.
- [flush_every under test harness with repeated setup/shutdown] → Mitigation: registry resets the flusher on shutdown (verified in spdlog 1.17 registry-inl.h); existing test pairing pattern already calls shutdown after every setup.
- [D6 retry still loses the crash report if the file stays unavailable] → Mitigation: tier-3 stderr is the ultimate sink; a crash report reaching stderr is recoverable from console capture and is strictly better than today's silent async drop.

## Migration Plan

Internal-only change; no external system. Rollback is a revert of the change's commits. The only user-visible behavioral delta is the scanner exit-code change (D3), which is intentional and documented in proposal.md — Impact.

## Open Questions

None — the one candidate (whether to also surface mid-run persistence failures to the user via console) is answered by the spec as-is: log-record only for mid-run `mark*` failures; console is reserved for top-level failures.
