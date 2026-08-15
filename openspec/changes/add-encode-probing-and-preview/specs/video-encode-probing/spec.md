## Purpose

Pre-encode quality probing that finds, per video, the highest CQ meeting a p5-percentile VMAF quality floor, presents the resulting encoding plan before the user confirms, and supports a dry-run mode.

## ADDED Requirements

### Requirement: Probing runs by default before the confirmation prompt

For every pending video encode (video mode, MP4 output), the system SHALL run a probing phase after scanning and output planning but before the encode confirmation prompt, unless the user specified `--crf`.

#### Scenario: Batch encode probes before prompting
- **WHEN** the user runs a batch encode without `--crf`
- **THEN** a probing phase runs for every pending video before the confirmation prompt is shown

#### Scenario: Explicit --crf skips probing
- **WHEN** the user runs an encode with `--crf N`
- **THEN** no probing runs, the confirmation prompt is shown directly, and the encode uses the fixed CQ N (current behavior)

### Requirement: Probe selects the highest CQ meeting the quality floor

The system SHALL probe each pending video at multiple candidate CQ values using short segments of the actual input and SHALL select the highest CQ whose p5-percentile VMAF is at or above the configured floor (default 95).

#### Scenario: Floor is reachable
- **WHEN** probing measures CQ 28 at p5-VMAF 95.8 and CQ 32 at p5-VMAF 94.6 with a floor of 95
- **THEN** the CQ where the floor is crossed (linearly interpolated and rounded down, i.e. 30 for these numbers) is selected and the encode uses that CQ

#### Scenario: Custom floor
- **WHEN** the user passes `--min-vmaf 90`
- **THEN** the highest CQ meeting p5-VMAF >= 90 is selected, which may be higher than with the default floor

### Requirement: Probing reflects production encode constraints

Probe encodes SHALL use the same encoder, preset, rate-control, maxrate, and quality-affecting settings as the real encode, so the measured quality and bitrate are representative of the final output.

#### Scenario: Resolution maxrate applies during probes
- **WHEN** the video's resolution maps to a maxrate cap
- **THEN** probe encodes are run with that same cap, and the measured quality reflects it

### Requirement: Probe flag validation

`--min-vmaf` SHALL accept values from 0 to 100 and SHALL be rejected outside that range. `--dry-run` combined with `--crf` SHALL fail with an error, because there is nothing to plan.

#### Scenario: Out-of-range quality floor
- **WHEN** the user passes `--min-vmaf 120`
- **THEN** the run fails with a validation error before any probing starts

#### Scenario: Dry-run with explicit crf
- **WHEN** the user passes `--dry-run` together with `--crf`
- **THEN** the run fails with a clear error stating that dry-run requires probing

### Requirement: Encoding plan is presented before the confirmation prompt

Before the confirmation prompt, the system SHALL print a per-file plan containing, for each pending video: the chosen CQ, the measured p5-VMAF, and the estimated output size, plus totals (aggregate estimated size and compression ratio). The plan SHALL flag files whose floor is unreachable.

#### Scenario: Plan precedes the prompt
- **WHEN** probing completes for a batch
- **THEN** a plan listing every pending file with its decision is printed, followed by the existing `(Y/n)` confirmation prompt

#### Scenario: Plan estimates size
- **WHEN** probing completes for a file
- **THEN** the plan line shows the estimated output size and the compression ratio against the source

### Requirement: Unreachable floor degrades to lowest-CQ with warning

When no probed CQ meets the floor, the system SHALL warn at plan time, encode with the lowest achievable CQ, and include the file in a "needs attention" list in the final summary.

#### Scenario: Maxrate cap blocks the floor
- **WHEN** even the lowest probed CQ yields p5-VMAF below the floor because the bitrate cap limits quality
- **THEN** the plan line is marked as a warning, the encode proceeds with the lowest probed CQ, and the file appears in the "needs attention" list of the final summary

### Requirement: Dry-run prints the plan and exits

`--dry-run` SHALL run the probing phase, print the plan, and exit without prompting or encoding. It SHALL NOT create any output files.

#### Scenario: Dry-run on a directory
- **WHEN** the user runs `encro <dir> --dry-run`
- **THEN** the probe plan is printed and the process exits successfully without creating encoded output files

### Requirement: Unattended runs keep the plan on record

With `--yes`, the system SHALL skip the interactive prompt but SHALL still run probing and print the plan before encoding starts.

#### Scenario: Yes-to-all batch
- **WHEN** the user runs an encode with `--yes`
- **THEN** probing runs, the plan is printed, and encoding starts without any interactive prompt

### Requirement: Probing is deterministic

For the same input file and the same encoding settings, probing SHALL produce the same CQ decision, so an interrupted and resumed run does not mix CQ values within one output file.

#### Scenario: Resume after interruption
- **WHEN** a run is interrupted during encoding and later resumed
- **THEN** re-probing produces the same CQ decision as the original run

### Requirement: Videos too short to probe use the default CQ

Videos shorter than the probing window budget SHALL skip probing and be encoded with the default CQ, without a warning.

#### Scenario: Very short video
- **WHEN** a pending video is shorter than the probing window budget
- **THEN** no probing runs and the video is encoded with the default CQ

### Requirement: Encode summary hints at preview

After a successful encode (with probing or without), the final summary SHALL print a one-line hint showing the preview command for the encoded files. `--dry-run` SHALL NOT print this hint.

#### Scenario: Summary carries the hint
- **WHEN** an encode run completes
- **THEN** the summary includes a hint line of the form `encro preview <original> <encoded>`

#### Scenario: Dry-run has no hint
- **WHEN** the user runs `--dry-run`
- **THEN** no preview hint is printed

### Requirement: Quality metric falls back to SSIM

When VMAF cannot be computed for a video (e.g. HDR input), probing SHALL fall back to SSIM with an equivalent p5 threshold for the same floor.

#### Scenario: HDR video
- **WHEN** the input is HDR or the VMAF computation is unavailable
- **THEN** probing uses SSIM to select the CQ instead of VMAF

### Requirement: Probing shows progress feedback

The probe phase SHALL show progress bars in the same style as the encode bars: one bar per worker slot (reused across files, showing the current file, CQ, and sub-step), plus an Overall bar when the batch exceeds the worker count. The terminal cursor SHALL be hidden while the bars render and restored afterwards; non-TTY output (pipes, tests, CI) SHALL render no bars.

#### Scenario: Batch probe shows per-slot bars
- **WHEN** the user runs a batch encode with more files than workers
- **THEN** the probe phase shows an Overall bar plus one bar per worker slot, and the bars update as each file's probe points complete

#### Scenario: No bars on non-TTY output
- **WHEN** output is captured by a pipe or a test harness
- **THEN** no progress bar sequences are emitted
