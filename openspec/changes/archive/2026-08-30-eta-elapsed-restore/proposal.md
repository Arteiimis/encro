## Why

The progress-bar ETA badge shows only a remaining-time estimate, and that estimate is
unavailable for the first ~30 s of an encode (seeding gate) and after every resume, leaving
the badge empty exactly when users start wondering about timing. Users also cannot see how
much time a resumed encode has already consumed: after resuming a job that ran for hours,
the badge gives no indication of the invested time.

## What Changes

- Progress-bar postfix badge changes from `[ETA 12m:34s]` to an elapsed/estimate badge
  `[12m:34s/1h:24m]`. Both parts share one duration formatter (`mm:ss` below one hour,
  `h:mm` at or above one hour — seconds intentionally dropped at hour granularity).
- While no estimate exists yet (startup ramp, resumed run before re-seed) the estimate part
  renders a `--:--` placeholder instead of hiding the whole badge. The badge is hidden only
  before the first progress sample (ffmpeg init) and at 100% completion.
- Elapsed time accumulates across interrupted-and-resumed encoding attempts: a new additive
  `encodedMs` field on the persisted task record is settled periodically during encoding and
  at run end, and the per-bar elapsed clock is seeded with it on resume. A file encoded for
  3 hours before an interrupt shows `[3h:05m/40m:00s]` after resuming, not `[0m:05m/...]`.
- Failed-then-retried encodes keep accumulating elapsed time (cumulative semantics stay
  pure; `attemptCount` already grows the same way). The estimate part remains per-run by
  design: it re-seeds from post-resume encoding speed.

## Capabilities

### New Capabilities
- `progress-eta-badge`: the progress-bar elapsed/estimate badge — display format and
  granularity rule, placeholder while unseeded, visibility rules, and cross-run elapsed
  accumulation for resumed or retried encodes backed by the job state store.

### Modified Capabilities

(none — no existing capability covers badge content; the existing
`progress-scroll-label` requirement that the ETA prefix stays pinned while the postfix
scrolls remains true unchanged under the new badge.)

## Impact

- `src/core/progress.h/.cpp`: badge assembly (new exported pure formatter replacing
  `formatEtaPart`), estimator elapsed accessor and elapsed base offset.
- `src/core/job_state.h`, `src/core/job_state.cpp`, `src/core/job_state_store.cpp`:
  additive persisted `encodedMs` field on `TaskRecord` (JSON schema grows one optional
  field; version stays 1; missing field reads as 0), settlement in
  `markProgress`/`markInterrupted`/`markFailed`/`markSucceeded`, reset in
  `clearExecutionState`.
- `src/video/video_batch_execution.h/.cpp`: thread the persisted elapsed into
  `resetEta` when a task starts.
- State file consumers: additive change only; older state files load unchanged.
