# video-preview Specification

## Purpose

A `preview` subcommand that generates a side-by-side comparison video from representative segments of an original video and its encoded output (or, with a single input, of the original against windows encoded with the probe-chosen configuration), scored per segment, and opens the result for visual verification.

## Requirements

### Requirement: Preview subcommand compares videos

`encro preview <original> [<encoded>]` SHALL accept one or two positional video inputs and generate a side-by-side comparison video. With two inputs, the original and the encoded output are compared. With one input, the source is probed and the comparison is rendered against windows encoded with the probe-chosen CQ, so the user can confirm the encode configuration's effect before committing to a full encode. Zero positionals SHALL fail with a clear error.

#### Scenario: Successful two-input preview
- **WHEN** the user runs `encro preview a.mp4 a.hevc.mp4` with two valid video files
- **THEN** a comparison video is generated and reported

#### Scenario: Single-input preview probes and compares
- **WHEN** the user runs `encro preview a.mp4` with one valid video file
- **THEN** the source is probed, its windows are encoded with the chosen CQ using the production encode settings, the windows are scored, and a comparison video is generated and reported

#### Scenario: Zero positionals fail
- **WHEN** the user runs `encro preview` with no positional arguments
- **THEN** the command fails with a clear missing-arguments error and a non-zero exit code

#### Scenario: Missing or invalid input fails
- **WHEN** any given input does not exist or is not a video file
- **THEN** the command fails with a clear error and a non-zero exit code
### Requirement: Representative segment selection

The system SHALL sample 5 uniformly spaced 10-second windows across the video duration and SHALL build the comparison video from those windows in time order. Videos shorter than the total window budget SHALL be compared in full.

#### Scenario: Uniform sampling across a long video
- **WHEN** the input is a 2-hour video
- **THEN** 5 windows of 10 seconds each, spread evenly across the timeline, are selected and concatenated in time order

#### Scenario: Short video falls back to full comparison
- **WHEN** the video is shorter than 50 seconds
- **THEN** the whole video is compared without sampling

### Requirement: Per-segment quality scoring

Each selected window SHALL be scored with the p5 quality metric computed between the original and the encoded file, following the same metric chain as the encode probe phase (primary metric with its declared fallbacks), and the scores SHALL be printed as a list in which the worst-scoring window is marked and each score is rendered in the units of the metric used.

#### Scenario: Score list marks the worst window
- **WHEN** the 5 windows are scored
- **THEN** a list prints each window's time range and score, with the lowest-scoring window explicitly marked

#### Scenario: Scores follow the active metric chain
- **WHEN** window scoring runs while the shared measurement helper is using its fallback metric
- **THEN** the printed window scores come from that same fallback metric and are rendered in its units

### Requirement: Comparison video carries identifying labels

The generated video SHALL show both streams side by side, labeled `ORIGINAL` and `ENCODED`, and each segment SHALL carry a label with its time range and quality score.

#### Scenario: Labels visible in output
- **WHEN** the comparison video is generated
- **THEN** the left half is labeled ORIGINAL, the right half ENCODED, and each segment shows its time range and score

### Requirement: Output defaults next to the original

When `--output` is omitted, the comparison video SHALL be written to the original video's directory as `<original-stem>.preview.mp4`, overwriting any existing file. `--output <path>` SHALL override the location.

#### Scenario: Default output location
- **WHEN** the user runs `encro preview videos/c.mp4 videos/c.hevc.mp4` without `--output`
- **THEN** the result is written to `videos/c.preview.mp4`

#### Scenario: Explicit output path
- **WHEN** the user passes `--output out.mp4`
- **THEN** the result is written to `out.mp4`

### Requirement: Result opens in the default player by default

After successful generation, the system SHALL open the comparison video with the system's default media player. `--no-open` SHALL suppress opening.

#### Scenario: Auto-open after generation
- **WHEN** preview generation completes without `--no-open`
- **THEN** the comparison video is opened in the default player

### Requirement: Manual window mode

`--start <seconds>` with `--duration <seconds>` SHALL skip segment sampling and compare exactly the requested time range, honoring the encoded video's length boundaries. A `--start` beyond the shorter input's duration SHALL fail with a clear error; a `--duration` that extends past the end SHALL be clamped to the remaining length.

#### Scenario: Focused comparison of a known range
- **WHEN** the user runs the preview with `--start 2510 --duration 20`
- **THEN** only the 20-second range starting at 2510s is compared, without sampling or scoring lists

#### Scenario: Out-of-range start fails
- **WHEN** the user passes `--start` beyond the shorter input's duration
- **THEN** the command fails with a clear error and a non-zero exit code

#### Scenario: Duration clamped at the end
- **WHEN** the requested range extends past the shorter input's end
- **THEN** the comparison is clamped to the remaining length

### Requirement: Stream alignment across sources

The comparison SHALL normalize both streams to the original's frame rate, scale mismatched resolutions down to the smaller dimensions, and trim to the shorter duration, so both halves stay frame-aligned.

#### Scenario: Variable-frame-rate source stays aligned
- **WHEN** the original has a variable frame rate
- **THEN** both streams are normalized to the original's average frame rate so the comparison stays in sync

#### Scenario: Differing resolutions are scaled
- **WHEN** the two inputs have different resolutions
- **THEN** both are scaled to the smaller dimensions for a coherent side-by-side view

### Requirement: Audio matches the compared windows

When the original contains an audio stream, the comparison video SHALL carry that audio trimmed to the same windows as the video and concatenated in the same order, so audio stays in sync with the shown segments. Audio codecs that the output container cannot copy SHALL be re-encoded to AAC. When the original has no audio, the result SHALL be silent.

#### Scenario: Windowed audio stays in sync
- **WHEN** the original has audio and 5 windows are compared
- **THEN** the comparison video plays the audio of exactly those 5 windows, trimmed and concatenated to match the video

#### Scenario: Incompatible audio codec is re-encoded
- **WHEN** the original's audio codec cannot be copied into the output container
- **THEN** the audio is re-encoded to AAC so the preview remains playable

### Requirement: Unsupported inputs fail clearly

Previewing an animated WebP or any non-video input SHALL fail with a clear "video comparison only" error.

#### Scenario: WebP rejected
- **WHEN** the user tries to preview an animated WebP against a video
- **THEN** the command fails with a clear error stating that preview supports video comparison only

### Requirement: Single-input mode uses the probe-chosen configuration

In single-input mode, the system SHALL run the probe phase on the source (same defaults, `--min-vmaf` and `--crf` bypass behavior as the encode path) and encode the preview windows with the production encode settings at the chosen CQ: same codec, preset-by-resolution, and maxrate as a real encode, differing only in CQ and output path. When the probe cannot determine a CQ (video shorter than the probe budget, scoring failure), the windows SHALL be encoded at the default CQ and the run SHALL note the fallback.

#### Scenario: Windows encoded at chosen CQ
- **WHEN** probing picks CQ N for the source
- **THEN** the preview windows are encoded with the same codec/preset/maxrate as a production encode at CQ N

#### Scenario: Probe failure falls back to default CQ
- **WHEN** probing cannot determine a CQ for the source
- **THEN** the preview renders with windows encoded at the default CQ 28 and an informational note is printed

#### Scenario: Scoring uses the shared quality measurement
- **WHEN** single-input mode scores a window
- **THEN** the score is measured with the shared segment quality helper (same metric chain as the two-input mode and the probe phase) exactly like those paths

### Requirement: Single-input preview shows one progress bar and reports after render

Single-input preview SHALL show a single progress bar spanning the whole pipeline — probe (0-40%), window encode+score (40-85%), render (85-100%) — in the same style as the encode bars, with the terminal cursor hidden while it renders (non-TTY output renders nothing). The summary (window list with the worst window marked and the output path) SHALL be printed only after the render completes.

#### Scenario: One bar spans probe through render
- **WHEN** the user runs single-input preview on a video that is probed
- **THEN** one progress bar advances through the probe sub-steps, the window encodes, and the render, then completes at 100%

#### Scenario: Summary appears after the render
- **WHEN** single-input preview finishes rendering
- **THEN** the window scores list and the written-to line are printed after the render completes, not before
