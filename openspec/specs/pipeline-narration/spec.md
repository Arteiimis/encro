# pipeline-narration Specification

## Purpose

Defines which narration lines each processing phase prints to the console and their wording conventions, so runs read as a sequence of outcome lines rather than a transcript of internal mechanics.

## Requirements

### Requirement: Scan narration is one line per phase on non-TTY output

When stdout is not a terminal, each scan phase SHALL print exactly one line: the completion line naming the input root and the count in user terms (`found 2 video(s) under <root>`, `found 3 picture(s) under <root>`, `found 3 file(s) under <root>`). The scan start line SHALL print only when stdout is a terminal, where it serves as live feedback during long recursive scans; it SHALL name the phase's activity without repeating the input root, which the completion line already carries. Scan lines SHALL NOT include code-literal qualifiers such as `(recursive=true)`. These defaults apply unless narration is suppressed wholesale by quiet mode (`logging-behavior`).

#### Scenario: Piped scan prints one line

- **WHEN** a video scan over a directory runs with stdout piped
- **THEN** stdout contains one scan line naming the input root and the count, and no separate start line

#### Scenario: Terminal scan keeps its start line

- **WHEN** the same scan runs on a terminal
- **THEN** the start line prints first as live feedback, followed by the completion line
- **AND** the start line does not repeat the input root the completion line names

### Requirement: Narration reports outcomes, not mechanics

Console narration SHALL NOT contain lines whose content is purely internal mechanics — concurrency scheduling (`Scheduling N video(s) with max M concurrent encode job(s)...`), batch grouping (`grouping into package batch(es)`), or plan preparation (`preparing pack plan...`). Phase activity is conveyed by progress bars on TTY output; the counts and results are conveyed by the phase's outcome lines.

#### Scenario: Encode batch start prints no concurrency line

- **WHEN** an encode batch starts scheduling work across worker slots
- **THEN** no line naming the concurrent job count prints to stdout

#### Scenario: Picture mode prints no grouping or plan-preparation lines

- **WHEN** a picture compress+pack run progresses from scan to packing
- **THEN** stdout contains no line about grouping into batches or preparing a pack plan

### Requirement: A phase is announced once

A processing phase SHALL NOT announce itself twice: a phase prints either an up-front announcement or its start line, not both (for example, picture compression prints `Compressing 3 picture(s) to JPEG (quality=2)...` and no separate "will be compressed to JPEG" announcement).

#### Scenario: Picture compression has one announcement

- **WHEN** a picture run with compression starts
- **THEN** exactly one line announces the compression phase, naming the count and quality

### Requirement: Narration wording conventions

Trailing status ellipses in narration lines SHALL use ASCII `...`. Report rule lines SHALL use one glyph family (`─`) across all tabular reports (encode plan, organize report); the mid-string filename truncation marker remains `…`, distinct from trailing ellipses.

#### Scenario: Status lines end with ASCII ellipsis

- **WHEN** any narration line ends with an ellipsis
- **THEN** the characters are `...` (three ASCII dots)

#### Scenario: Reports share one rule glyph

- **WHEN** the encode plan and the organize report each render their rule lines
- **THEN** both use the same `─` glyph

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

### Requirement: Phase summary lines state their own elapsed time

Each phase summary line SHALL state the wall time that phase itself spent working, measured from the moment its phase began to the moment its bars were cleared, excluding user prompts and every other phase. The duration SHALL render as `<n>s` below one minute, `<n>m:<ss>` below one hour, and `<n>h:<mm>` from one hour. Progress-bar badges keep their own fixed-width form (`progress-eta-badge`), which is unaffected by this change.

#### Scenario: Sub-minute phase reads in seconds

- **WHEN** a phase finishes 24 seconds after it began
- **THEN** its summary line ends with `in 24s`

#### Scenario: Multi-minute phase reads in minutes and seconds

- **WHEN** a phase finishes after 12 minutes and 34 seconds
- **THEN** its summary line ends with `in 12m:34s`

#### Scenario: Hour-scale phase drops the seconds

- **WHEN** a phase finishes after 3 hours and 5 minutes
- **THEN** its summary line ends with `in 3h:05m`

#### Scenario: Prompt waiting is not counted

- **WHEN** a phase begins after a confirmation prompt the user left unanswered for a minute
- **THEN** the elapsed time the phase line states does not include that waiting time
