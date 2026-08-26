## ADDED Requirements

### Requirement: Quality metric chain falls back through VMAF and SSIM

Probe scoring SHALL use XPSNR as the primary quality metric for non-HDR inputs. When XPSNR cannot be computed (e.g. an ffmpeg build without the filter) the system SHALL fall back to VMAF, and when VMAF also cannot be computed it SHALL fall back to SSIM, emitting a warning naming the degraded metric on each fallback. HDR inputs keep using SSIM directly. Every fallback decision SHALL be recorded so plan output labels the score with the metric actually used.

#### Scenario: Primary scoring path uses XPSNR

- **WHEN** a non-HDR video is probed with an ffmpeg build providing the xpsnr filter
- **THEN** every probe point is scored with XPSNR and the plan shows p5 in XPSNR dB labeled as such

#### Scenario: XPSNR unavailable degrades to VMAF

- **WHEN** probe scoring runs against an ffmpeg build without the xpsnr filter
- **THEN** a warning names XPSNR as unavailable, scoring proceeds with VMAF, and the plan labels the scores as VMAF

#### Scenario: VMAF and XPSNR both unavailable degrade to SSIM

- **WHEN** neither XPSNR nor VMAF can be computed for a segment
- **THEN** a warning names the failing metrics and scoring falls back to SSIM exactly as the pre-existing SSIM fallback did

## MODIFIED Requirements

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

### Requirement: Encoding plan is presented before the confirmation prompt

Before the confirmation prompt, the system SHALL print a per-file plan containing, for each pending video: the chosen CQ, the measured p5 quality score rendered in the units of the metric actually used for that file, and the estimated output size, plus totals (aggregate estimated size and compression ratio). The plan SHALL flag files whose floor is unreachable.

#### Scenario: Plan precedes the prompt

- **WHEN** probing completes for a batch
- **THEN** a plan listing every pending file with its decision is printed, followed by the existing `(Y/n)` confirmation prompt

#### Scenario: Plan estimates size

- **WHEN** probing completes for a file
- **THEN** the plan line shows the estimated output size and the compression ratio against the source

#### Scenario: Mixed-metric batch renders each row in its own units

- **WHEN** a batch contains files scored with different metrics (e.g. one HDR file scored with SSIM and non-HDR files scored with XPSNR)
- **THEN** each plan row renders its p5 value formatted for that file's metric instead of forcing one unit onto all rows, and the plan header describes the floor in its stable VMAF-scale meaning rather than deriving its metric name from any single file's metric

### Requirement: Unreachable floor degrades to lowest-CQ with warning

When no probed CQ meets the floor, the system SHALL warn at plan time, encode with the lowest achievable CQ, and include the file in a "needs attention" list in the final summary.

#### Scenario: Maxrate cap blocks the floor

- **WHEN** even the lowest probed CQ yields a p5 score below the mapped floor threshold because the bitrate cap limits quality
- **THEN** the plan line is marked as a warning, the encode proceeds with the lowest probed CQ, and the file appears in the "needs attention" list of the final summary

## REMOVED Requirements

### Requirement: Quality metric falls back to SSIM

**Reason**: Superseded by the multi-step fallback chain; XPSNR replaces VMAF as the primary metric, so the single-level fallback no longer describes observed behavior.

**Migration**: Behavior is preserved by "Quality metric chain falls back through VMAF and SSIM"; HDR still skips directly to SSIM.
