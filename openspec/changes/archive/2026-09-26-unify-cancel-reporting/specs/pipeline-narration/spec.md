## MODIFIED Requirements

### Requirement: Every bar-rendering phase ends with one summary line

Every phase that renders progress bars SHALL print exactly one summary line, after its bars are cleared (`progress-bar-lifecycle`). A phase that ends before its work completes — a stop request, or a failure that aborts the phase — prints no summary line, and its bars are cleared first. A phase aborted by a stop request prints the single cancellation notice `cancellation-reporting` requires and nothing else of its own; a phase aborted by a failure prints whatever that failure produces. Phases that process a counted set of items state their counts, and a phase that can also fail or skip items appends one segment per non-empty class; phases that write a user-facing artifact name it; every phase's line ends with the phase's own elapsed time (`Phase summary lines state their own elapsed time`):

- `Probed 8/8 videos in 24s`, or with files that skipped probing: `Probed 7/8 videos (1 not probed) in 24s`
- `Encoded 8/8 videos → D:\out in 12m:34s`, or with failures: `Encoded 5/8 videos (2 failed, 1 skipped) → D:\out in 12m:34s`
- `Packed 1 archive(s) → D:\out\packed in 14s`
- `Compressed 12/12 pictures in 8s`
- `Converted 12/12 videos to WebP in 42s`
- `Preview written to: D:\preview.mp4 in 35s`

A class segment SHALL print only when that class is non-empty: a run with no failures and no skipped files prints neither `(n failed)` nor `(n skipped)`. The failure and skip classes SHALL be the only counts in this segment; per-file detail stays in the failed-file list, the plan table, and the log.

A phase's per-item detail lines belong to that phase's output and sit around its summary line: the failed-file list and the attention block follow the encode line, and the preview's window list precedes the preview line. The probing phase prints its line above its plan block, which is the phase's product output and not a second summary line; its collapsed form (`plan-output-formatting`) states its batch count and skip reason in place of the `<succeeded>/<total>` shape and is itself the phase's summary line. A phase whose product output is a report — the image-organize report — is exempt: the report is the phase's own output and the phase clears its bars before printing it.

#### Scenario: Successful phase prints one line without class segments

- **WHEN** a phase completes with every item succeeding and no items skipped
- **THEN** its summary line states only the counts, and for file-producing phases the destination

#### Scenario: Failures and skips are named in the line

- **WHEN** an encode ends with 5 of 8 files encoded, 2 failed, and 1 skipped because its estimated size exceeded its source
- **THEN** the summary line reads `Encoded 5/8 videos (2 failed, 1 skipped) → <destination> in <elapsed>`
- **AND** the failed-file list follows on the lines below it

#### Scenario: Table-producing phase prints the line before its block

- **WHEN** the probing phase completes with measurements
- **THEN** its summary line prints above the plan block, and no additional probing-completion line prints after the table

#### Scenario: Preview prints its line after the window list

- **WHEN** a preview run finishes rendering
- **THEN** the window list prints first and the written-to summary line follows it

#### Scenario: Aborted phase prints no summary line

- **WHEN** an encode batch is interrupted by a stop request
- **THEN** its bars are cleared and no encode summary line prints

#### Scenario: Stopped phase prints only its cancellation notice

- **WHEN** a stop request aborts a phase that renders progress bars
- **THEN** the phase prints `warning: <Stage> canceled by user.` on stderr, no summary line for itself, and no per-item failure or skip line
