## Context

The ETA estimator (`EtaEstimator` in `src/core/progress.h/.cpp`) already tracks an anchor
time (`startAt_`, set at the first positive-progress sample) and a baseline percent
(`baseProgress_`) per bar; `resetEta` clears them at every `barEncodingStart`. The job
state store (`src/core/job_state_store.cpp`) persists per-task records with
`startedAtMs` — but `markRunning` overwrites it on every attempt, so no cumulative
encoding time survives a restart today. The postfix rendering path is
`ProgressContext::applyBarText` → `fitPostfixWithEta`, which already pins a badge prefix
while the label scrolls and sizes it dynamically via display width.

See proposal.md for motivation and specs/progress-eta-badge/spec.md for the behavior
contract.

## Goals / Non-Goals

**Goals:**

- Badge `[<elapsed>/<estimate>]` with one shared duration formatter and the
  magnitude-based granularity rule.
- `--:--` placeholder for the estimate part while unseeded.
- Cumulative elapsed across interrupted and retried attempts, persisted in the job state
  file, restored into the per-bar elapsed clock on resume.

**Non-Goals:**

- Restoring or persisting the *estimate* — it stays per-run and re-seeds from post-resume
  speed (by design: the two numbers answer different questions).
- Elapsed restore for non-encode task kinds (pictures, archive, preview) — their bars
  keep per-run elapsed; only `encode_video` tasks carry persisted accumulation.
- Changing the scroll/width behavior of the postfix (`progress-scroll-label` stays valid).

## Decisions

### D1: Elapsed clock lives in the estimator, base offset injected at reset

`EtaEstimator` gains `elapsedBaseSec_` (default 0) and `elapsedSeconds()` returns
`base + (now - startAt_)`; `ProgressContext::resetEta(barIndex, elapsedBaseSec = 0)`
forwards it. `barEncodingStart` reads the task record's accumulated time and passes it
in.

*Why the estimator owns it:* the anchor semantics already live there (first
positive-progress sample), rendering call sites (`setProgress`, `tick`,
`setPostfixText`) already route through `applyBarText`, and the badge needs elapsed at
render time, not sample time. *Alternative:* track elapsed in `EncodingState` (it has
`startTime`) and format at the call site — rejected: the call site would have to
replicate the placeholder/visibility rules and the anchor semantics would split across
two owners.

### D2: Accumulated time persisted as `encodedMs` on `TaskRecord`

Additive optional `std::int64_t encodedMs`. Settlement points:

- `markProgress` — already called by the monitor roughly every parse pass and persisted
  through the existing throttled flush; fold in `encodedMs = prior + (now - startedAtMs)`
  there. No new write cadence; a crash loses at most one settlement interval.
- `markInterrupted`, `markFailed`, `markSucceeded` — final settle so the stored value is
  exact at run end.

`clearExecutionState` (fresh restart, fingerprint mismatch) resets it to 0 together with
the other execution fields. JSON stays version 1: readers treat a missing field as 0,
older binaries ignore it.

*Alternative considered:* persisting only the original `startedAtMs` and computing
`now - startedAtMs` — rejected: it counts idle time between attempts as consumed time
(an overnight pause would show absurd elapsed values).

### D3: Cumulative across failures, not just interrupts

`markRunning` already bumps `attemptCount` and restarts `startedAtMs`; accumulation is
unconditional. A failed attempt's burned time is real cost, and keeping one pure rule
avoids a per-status branch in the settlement code. (User decision recorded in the
proposal.)

### D4: One duration formatter, per-part granularity

`formatEtaPart` is replaced by an exported pure `formatEtaBadge(elapsedSec, etaSec)` ->
`optional<string>`: nullopt elapsed => no badge; nullopt estimate => `--:--` placeholder.
Both parts render through the same `mm:ss` / `h:mm` rule, evaluated independently, so
mixed forms like `[3h:05m/40m:00s]` are unambiguous via the `h`/`m`/`s` suffixes.
Ceiling rounding is kept from the existing formatter so a running encode never shows
`0m:00s` while work remains.

### D5: Badge visibility gates stay in `applyBarText`

`applyBarText` renders the badge only when the estimator is anchored and progress < 100;
`fitPostfixWithEta` is unchanged (it already sizes the pinned prefix dynamically; the new
badge is ~3 columns wider than the old one, which only trims the scroll window).

## Risks / Trade-offs

- [Extra JSON writes] Settlement folds into the existing `markProgress` persist cadence
  (throttled flush) → no new I/O pattern; a hard crash can lose the last settlement
  interval, bounded by the flush throttle.
- [Schema drift] Additive optional field with missing-reads-as-0 → old state files and
  old binaries both keep working; no migration needed.
- [Elapsed vs estimate semantics mismatch on resume] `[3h:05m/40m:00s]` mixes cumulative
  elapsed with a fresh estimate → deliberate (recorded in specs); the estimate converges
  within the normal seeding window.
- [Slot bars reused across files] The elapsed base is injected per `barEncodingStart` via
  the task's action id, so a reused bar always restarts with the new file's accumulated
  time, not the previous file's.

## Migration Plan

Additive change; no migration. Rollback = revert commit (the new JSON field is ignored by
older builds).

## Open Questions

None.
