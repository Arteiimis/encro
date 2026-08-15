## Purpose

A `preview` subcommand that generates a side-by-side comparison video from representative segments of an original video and its encoded output (or, with a single input, of the original against windows encoded with the probe-chosen configuration), scored per segment, and opens the result for visual verification.

## MODIFIED Requirements

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

## ADDED Requirements

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
- **THEN** the score is measured with the shared segment quality helper (VMAF, SSIM fallback for HDR or unavailable VMAF) exactly like the two-input mode and the probe phase

### Requirement: Single-input preview shows one progress bar and reports after render

Single-input preview SHALL show a single progress bar spanning the whole pipeline — probe (0-40%), window encode+score (40-85%), render (85-100%) — in the same style as the encode bars, with the terminal cursor hidden while it renders (non-TTY output renders nothing). The summary (window list with the worst window marked and the output path) SHALL be printed only after the render completes.

#### Scenario: One bar spans probe through render
- **WHEN** the user runs single-input preview on a video that is probed
- **THEN** one progress bar advances through the probe sub-steps, the window encodes, and the render, then completes at 100%

#### Scenario: Summary appears after the render
- **WHEN** single-input preview finishes rendering
- **THEN** the window scores list and the written-to line are printed after the render completes, not before
