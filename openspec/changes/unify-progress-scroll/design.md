## Context

See proposal.md — Why. Current state that shapes the approach (verified by reading the code):

- The scroll offset is a pure function of wall-clock milliseconds — `bounceOffset` (progress.cpp:163) with 8 cols/s and a 1 s pause at each end (:65-69) — so a *late* repaint already lands at the correct offset; the missing piece is only the repaint trigger.
- Repaints happen only inside the setters — `setPostfixText` (:348), `setProgress` (:355), and `setTone` (:385) each end in `render()` (:394) — so a bar repaints when data arrives and never on its own. `ProgressContext::tick()` (:376) is the repaint-only entry point. Its only production caller is `monitorEncodingProgress` (video_encoding_state.cpp:245-248), gated by `kScrollTickInterval` = 100 ms (:29), added by commit `cafee70` for the encode path only.
- The probe phase runs through `taskexec::runTasks` (task_executor.cpp:86-121), which contains no timer at all — all probe/preview/pack repaints are event-driven (probe steps and window scores take seconds), so their marquees freeze between events.
- Pack keeps two 120 ms spinner threads that repaint as a side effect of rotating their frame character (packer.cpp:344-362, pack_service.cpp:110-150) — ad-hoc timers of the same kind — but neither fires outside its finalize window, so pack bars still freeze during long per-file work.
- Postfix parts are split on `|` with surrounding whitespace trimmed (:79-92), and `fitPostfixWithEta` (:193) re-joins everything after the first part with the hardcoded ` | ` delimiter (`kPostfixDelim` :69). Probe/preview postfixes use `·` (encode_probe.cpp:546,558; preview_process.cpp:565,576), so the splitter sees one part and scrolls the status away.
- Constraints to respect: `progressBarsAllowed()` gates both renders and erases (progress.cpp:20-23; render gate :398, erase gate :405); `eraseBars()` documents "no render call may follow until the context is destroyed" (progress.h:88-99); tests must synchronize by polling with `testutils::waitUntil`, no fixed sleeps (AGENTS.md sync convention).

## Goals / Non-Goals

**Goals:**

- One repaint clock, owned by `progress::ProgressContext`, so every bar in every mode animates by the same rule with no per-command timer.
- One postfix grammar — ` | ` — for label vs pinned status, in every mode.
- Keep the existing display contract untouched: layout budget, bar width, scroll speed, pause duration, ETA math, non-TTY silence.
- A test seam that proves the clock runs without a TTY and without sleeping.

**Non-Goals:**

- Coalescing setter renders into the repaint clock (setters keep rendering immediately; adding up to 100 ms of status latency to save terminal writes is not worth it here).
- Per-bar clocks or desynchronized scroll phases (all bars stay in lockstep, as accepted in the 2026-08-02 design).
- Replacing the pack finalize spinner frames or giving the picture counters a status tail.
- Changing scroll speed/appearance tuning constants.

## Decisions

**D1 — The repaint clock moves into `ProgressContext`; the wall-clock offset stays.**

`ProgressContext` starts a `std::jthread` when its first bar is added and stops it in `eraseBars()` and in member destruction; the thread wakes every 100 ms and calls `tick()` — the same cadence the encode monitor used, and the same repaint-only semantics.

Alternatives considered:
- *Keep one timer per caller* (today): the probe phase has no loop at all, so it would need a new bespoke thread and lifetime, and preview/pack/picture would stay inconsistent — the same mistake that produced this bug.
- *Drive repaints from `taskexec::runTasks`*: fixes probe only, leaves the sequential pack/preview paths and their bars frozen.
- *Advance the offset per repaint instead of per wall-clock* (rejected in 2026-08-02, D1): the pace would then vary with the event rate, so sparse events would scroll *slower* instead of jumping to the right place — it converts a visibility bug into a speed bug.

**D2 — Lifetime and locking rules for the ticker.**

- `ticker_` is declared after `mtx_` in the class, so member destruction order joins the thread before the mutex dies; no explicit destructor is needed.
- `eraseBars()` stops the ticker *before* acquiring `mtx_` — joining while holding the lock could deadlock against a ticker blocked in `tick()` on the same mutex. This keeps the existing "no render after eraseBars" contract.
- The loop sleeps in 100 ms chunks and exits on `stop_token`; shutdown can therefore lag up to one interval. Accepted: `eraseBars()` is followed by a phase summary or process exit, so a ≤100 ms join is invisible. A condition-variable wakeup would remove the lag at the cost of extra machinery.
- The ticker starts even when stdout is not a terminal (so tests can observe it), but `tick()` returns before doing per-bar work when `progressBarsAllowed()` is false — the non-TTY run pays one wakeup per interval and nothing else.

**D3 — Unify on ` | ` in the probe/preview call sites, not in the splitter.**

The 4 postfixes using `·` change to ` | ` (`Probing: <file> | CQ <cq> <phase>`, `... | CQ <cq> scored`). The alternative — teaching `splitPostfixParts` a delimiter set and preserving the original separators — costs more code and cannot preserve the look anyway: `fitPostfixWithEta` re-joins the tail with the hardcoded ` | ` (progress.cpp:69), so a `·`-split tail would be rendered as ` | ` on the first repaint. Widening the delimiter set also adds ambiguity with the pack spinner, which emits a literal `|` frame inside its tail (packer.cpp:352-357).

**D4 — A read-only repaint counter and estimate view are the test seams.**

`ProgressContext` gains a repaint counter (wakeups counted before the TTY gate) and an `etaSeconds()` view of the bar's current remaining-time estimate, both framed like the existing `elapsedSeconds()` diagnostics accessor. Together they let a `[progress]` test poll with `testutils::waitUntil` until the counter grows — proving the clock, not just the pure `bounceOffset` — and assert that repaints neither change the progress value nor re-seed the estimate. Needed because indicators 2.3 exposes no getter for a bar's progress or postfix text, so without these views that clause of the spec would be unverifiable prose. Rejected: a test-only enable/short-interval hook (test-only API that production never exercises), and forcing a fake TTY (no such facility in the test utils, and `render()` would still be the thing being tested).

## Risks / Trade-offs

- [One extra thread per progress context] → Contexts are per-phase and short-lived; a context with no bars never starts one. Non-TTY contexts spin one wakeup per 100 ms doing nothing, and `tick()` short-circuits before per-bar work.
- [Deadlock if the ticker is joined while `mtx_` is held] → `eraseBars()` stops it before locking, and destruction relies on member order; both get a comment.
- [Higher terminal write volume in probe/pack (10 Hz × bars × lines)] → This is exactly what the encode path already does, and repaints stop when the context is erased; the alternative (coalescing renders) is a non-goal.
- [Repaints now refresh the ETA badge in probe/preview/pack too] → Intended: the badge is a clock; the ETA requirement constrains format and visibility, not refresh cadence, so no spec delta is needed.
- [Postfix grammar is a convention, not enforced] → Callers can still pass any string; the grammar requirement documents the rule and the tests pin the probe/preview strings, which is the same level of enforcement the existing spec has.

## Migration Plan

Pure in-process terminal display change: no config keys, state files, CLI surface, or log/JSON output affected. Rollback = revert the commit; the only externally visible artifacts are bar frames.
