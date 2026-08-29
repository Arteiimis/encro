## Purpose

Shows a combined elapsed/estimate badge on progress bars so users can see how long an
encode has been running (including time spent in earlier interrupted or failed attempts)
next to the remaining-time estimate, with a placeholder while the estimate is still
seeding.

## ADDED Requirements

### Requirement: Elapsed/estimate badge format

When a progress bar has progress to report (at least one progress sample taken, percent
below 100), the system SHALL render an elapsed/estimate badge in the postfix formatted as
`[<elapsed>/<estimate>]`. Both durations SHALL use the same granularity rule: `mm:ss`
below one hour, `h:mm` at or above one hour (seconds dropped at hour granularity).

#### Scenario: Mid-encode badge

- **WHEN** an encoding bar has an elapsed time of 754 seconds and a seeded estimate of
  5020 seconds
- **THEN** the badge renders as `[12m:34s/1h:24m]`

#### Scenario: Granularity switches at one hour independently per part

- **WHEN** the elapsed time is 11100 seconds and the estimate is 2410 seconds
- **THEN** the badge renders as `[3h:05m/40m:10s]` (elapsed drops seconds, estimate keeps
  them)

### Requirement: Placeholder while the estimate is unseeded

When elapsed time is known but no remaining-time estimate exists yet (startup ramp, or a
resumed run before the estimator re-seeds), the badge SHALL render the estimate part as
the placeholder `--:--` instead of hiding the badge.

#### Scenario: Resumed run before re-seed

- **WHEN** a resumed encode has been running for 30 seconds and the estimator has not yet
  seeded an estimate
- **THEN** the badge renders as `[0m:30s/--:--]`

### Requirement: Badge visibility

The badge SHALL NOT be rendered before the first progress sample of a run (ffmpeg
initialization, no elapsed clock yet) and SHALL NOT be rendered once the bar reaches 100
percent (the completion postfix takes over).

#### Scenario: Initialization window

- **WHEN** an encoding has started but no progress sample has been taken yet
- **THEN** no badge is rendered and the postfix shows only the task label

#### Scenario: Completion

- **WHEN** a bar reaches 100 percent
- **THEN** no badge is rendered

### Requirement: Elapsed time accumulates across attempts

For a resumable encode task, elapsed time SHALL accumulate across encoding attempts:
time spent in an interrupted attempt plus time spent in the current attempt, excluding
idle time between attempts. A retried encode after a failure SHALL also keep
accumulating. The accumulated time SHALL be persisted so it survives process restarts,
and SHALL be reset only when the task's execution state is cleared.

#### Scenario: Resumed encode shows total consumed time

- **WHEN** an encode ran for 3 hours, was interrupted, and the resumed run has been
  encoding for 5 minutes
- **THEN** the badge shows an elapsed time of approximately 3 hours 5 minutes (not 5
  minutes)

#### Scenario: Idle time between attempts is not counted

- **WHEN** an encode was interrupted at 3 hours and resumed the next day
- **THEN** the elapsed time shown after resuming excludes the idle gap and equals the
  previously accumulated encoding time plus the current run's encoding time

#### Scenario: Accumulation survives a process restart

- **WHEN** the accumulated encoding time is persisted, the process exits, and the job is
  later resumed in a new process
- **THEN** the elapsed badge continues from the persisted accumulated time
