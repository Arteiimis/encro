# video-encode-probing Specification

## Purpose

Pre-encode quality probing that finds, per video, the highest CQ meeting a p5-percentile quality floor on the VMAF-equivalent scale (`--min-vmaf`), presents the resulting encoding plan before the user confirms, and supports a dry-run mode.

## Requirements

### Requirement: Probing runs by default before the confirmation prompt

For every pending video encode (video mode, MP4 output), the system SHALL run a probing phase after scanning and output planning but before the encode confirmation prompt, unless the user specified `--crf`.

#### Scenario: Batch encode probes before prompting
- **WHEN** the user runs a batch encode without `--crf`
- **THEN** a probing phase runs for every pending video before the confirmation prompt is shown

#### Scenario: Explicit --crf skips probing
- **WHEN** the user runs an encode with `--crf N`
- **THEN** no probing runs, the confirmation prompt is shown directly, and the encode uses the fixed CQ N (current behavior)

### Requirement: Probe selects the highest CQ meeting the quality floor

The system SHALL probe each pending video at multiple candidate CQ values using short segments of the actual input and SHALL select the highest CQ whose p5-percentile quality score meets the configured floor (default 95). The floor keeps its VMAF-scale meaning (`--min-vmaf`, 0–100); comparisons against non-VMAF measurements SHALL use a fixed global anchor mapping from VMAF floors onto equivalent metric thresholds, interpolated linearly between supported floor values. Anchor-supported VMAF floors are 90, 95, and 97; floors below 90 use the lowest anchor and floors above 97 the highest.

#### Scenario: Floor is reachable
- **WHEN** probing measures CQ 28 at a p5 score above the floor threshold and CQ 32 below it with a floor of 95
- **THEN** the CQ where the threshold is crossed (linearly interpolated and rounded down) is selected and the encode uses that CQ

#### Scenario: Custom floor
- **WHEN** the user passes `--min-vmaf 90`
- **THEN** the highest CQ meeting the mapped threshold for VMAF-equivalent 90 is selected, which may be higher than with the default floor

#### Scenario: In-between floor maps to interpolated threshold
- **WHEN** the user passes a floor value between two supported VMAF anchors (e.g. `--min-vmaf 93`)
- **THEN** the comparison threshold is linearly interpolated between the two neighboring anchor thresholds

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

Before the confirmation prompt, the system SHALL print a per-file plan containing, for each pending video: the chosen CQ, the measured p5 quality score rendered in the units of the metric actually used for that file, and the estimated output size, plus totals (aggregate estimated size and compression ratio). The plan SHALL flag files whose floor is unreachable.

#### Scenario: Plan precedes the prompt
- **WHEN** probing completes for a batch
- **THEN** a plan listing every pending file with its decision is printed, followed by the existing `(Y/n)` confirmation prompt

#### Scenario: Plan estimates size
- **WHEN** probing completes for a file
- **THEN** the plan line shows the estimated output size and the compression ratio against the source

#### Scenario: Mixed-metric batch renders each row in its own units
- **WHEN** a batch contains files scored with different metrics (e.g. one HDR file scored with SSIM and non-HDR files scored with VMAF)
- **THEN** each plan row renders its p5 value formatted for that file's metric instead of forcing one unit onto all rows, and the plan header describes the floor in its stable VMAF-scale meaning rather than deriving its metric name from any single file's metric

### Requirement: Unreachable floor degrades to lowest-CQ with warning

When no probed CQ meets the floor, the system SHALL warn at plan time, encode with the lowest achievable CQ, and include the file in a "needs attention" list in the final summary.

#### Scenario: Maxrate cap blocks the floor
- **WHEN** even the lowest probed CQ yields a p5 score below the mapped floor threshold because the bitrate cap limits quality
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

### Requirement: Quality metric chain falls back through XPSNR and SSIM

Probe scoring SHALL use VMAF as the primary quality metric for non-HDR inputs: the `--min-vmaf` floor is defined on the VMAF scale, so decisions compare natively without cross-metric mapping. When VMAF cannot be computed (e.g. an ffmpeg build without libvmaf) the system SHALL fall back to XPSNR, comparing against the VMAF-equivalent XPSNR floor, and when XPSNR also cannot be computed it SHALL fall back to SSIM, emitting a warning naming the degraded metric on each fallback. HDR inputs keep using SSIM directly. Every fallback decision SHALL be recorded so plan output labels the score with the metric actually used.

#### Scenario: Primary scoring path uses VMAF
- **WHEN** a non-HDR video is probed with an ffmpeg build providing libvmaf
- **THEN** every probe point is scored with VMAF and the plan shows p5 on the VMAF scale labeled as such

#### Scenario: VMAF unavailable degrades to XPSNR
- **WHEN** probe scoring runs against an ffmpeg build without libvmaf but with the xpsnr filter
- **THEN** a warning names VMAF as unavailable, scoring proceeds with XPSNR against the VMAF-equivalent XPSNR floor, and the plan labels the scores as XPSNR

#### Scenario: VMAF and XPSNR both unavailable degrade to SSIM
- **WHEN** neither VMAF nor XPSNR can be computed for a segment
- **THEN** a warning names the failing metrics and scoring falls back to SSIM exactly as the pre-existing SSIM fallback did

### Requirement: Probing shows progress feedback

The probe phase SHALL show progress bars in the same style as the encode bars: one bar per worker slot (reused across files, showing the current file, CQ, and sub-step), plus an Overall bar when the batch exceeds the worker count. The terminal cursor SHALL be hidden while the bars render and restored afterwards; non-TTY output (pipes, tests, CI) SHALL render no bars.

#### Scenario: Batch probe shows per-slot bars
- **WHEN** the user runs a batch encode with more files than workers
- **THEN** the probe phase shows an Overall bar plus one bar per worker slot, and the bars update as each file's probe points complete

#### Scenario: No bars on non-TTY output
- **WHEN** output is captured by a pipe or a test harness
- **THEN** no progress bar sequences are emitted

### Requirement: Probe results persist across runs

The system SHALL reuse a previously measured probe decision for an input file when the input file and all decision-affecting settings are unchanged, instead of re-measuring it. A reused decision SHALL be marked as cached in the encoding plan. When the input file or any decision-affecting setting changes, the cached decision SHALL be discarded and probing SHALL re-run for that file.

#### Scenario: Unchanged batch re-run
- **WHEN** the user runs an encode on a batch of videos that were probed in an earlier run, with the same settings
- **THEN** probing is skipped for the unchanged files and their plan lines show the cached CQ marked as cached

#### Scenario: Input file modified
- **WHEN** a previously probed input file has changed (different size or modification time) since it was probed
- **THEN** its cached decision is discarded and the file is probed again

#### Scenario: Decision settings changed
- **WHEN** the user changes a decision-affecting setting (quality floor, codec, encoder preset, or quality metric) between runs
- **THEN** cached decisions from the earlier configuration are not reused and the files are probed again

#### Scenario: Cached decision participates in all plan paths
- **WHEN** a file's decision is reused from the cache during a `--dry-run` or `--yes` run
- **THEN** the plan prints the cached decision marked as cached, and the run otherwise behaves identically to a freshly probed run

#### Scenario: Files without a measurement are never cached
- **WHEN** a pending video is not actually measured (too short to probe, or probing fails for it) and is encoded with the default CQ
- **THEN** no cache entry is created for it and the plan never marks it as cached

#### Scenario: Probing bypass paths do not touch the cache
- **WHEN** the user passes `--crf` (probing is skipped entirely) or a video is dropped before probing because it is already HEVC-encoded
- **THEN** no cache entry is read or written for those videos

#### Scenario: Resume after interruption with a cache hit
- **WHEN** a run is interrupted during encoding, then resumed, and the cached decision for a file matches its re-probe key
- **THEN** the resumed run uses the same CQ as the original run (cache hit and re-probe agree)
