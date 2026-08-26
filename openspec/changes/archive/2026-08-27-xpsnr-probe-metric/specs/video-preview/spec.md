## MODIFIED Requirements

### Requirement: Per-segment quality scoring

Each selected window SHALL be scored with the p5 quality metric computed between the original and the encoded file, following the same metric chain as the encode probe phase (primary metric with its declared fallbacks), and the scores SHALL be printed as a list in which the worst-scoring window is marked and each score is rendered in the units of the metric used.

#### Scenario: Score list marks the worst window

- **WHEN** the 5 windows are scored
- **THEN** a list prints each window's time range and score, with the lowest-scoring window explicitly marked

#### Scenario: Scores follow the active metric chain

- **WHEN** window scoring runs while the shared measurement helper is using its fallback metric
- **THEN** the printed window scores come from that same fallback metric and are rendered in its units

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
