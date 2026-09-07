## MODIFIED Requirements

### Requirement: Encoding plan is presented before the confirmation prompt

Before the confirmation prompt, the system SHALL print a per-file plan containing, for each pending video: the chosen CQ, the measured p5 quality score rendered in the units of the metric actually used for that file, and the estimated output size. When any file carries measured data, the plan SHALL also print totals (aggregate estimated size and compression ratio); totals SHALL NOT print when no estimates exist. The plan SHALL flag files whose floor is unreachable. When no pending file carries measured probe data, the plan SHALL render as the collapsed single-line form defined by `plan-output-formatting`. No standalone probing-completion line SHALL print in addition to the plan.

#### Scenario: Plan precedes the prompt

- **WHEN** probing completes for a batch
- **THEN** a plan listing every pending file with its decision is printed, followed by the existing `(Y/n)` confirmation prompt, and no separate line announcing probing completion prints

#### Scenario: Plan estimates size

- **WHEN** probing completes for a file
- **THEN** the plan line shows the estimated output size and the compression ratio against the source

#### Scenario: Totals are omitted without estimates

- **WHEN** the batch carries no measured probe data
- **THEN** no totals line prints, and no ratio is computed against an empty estimate

#### Scenario: Mixed-metric batch renders each row in its own units

- **WHEN** a batch contains files scored with different metrics (e.g. one HDR file scored with SSIM and non-HDR files scored with VMAF)
- **THEN** each plan row renders its p5 value formatted for that file's metric instead of forcing one unit onto all rows, and the plan header describes the floor in its stable VMAF-scale meaning rather than deriving its metric name from any single file's metric

### Requirement: Videos too short to probe use the default CQ

Videos shorter than the probing window budget SHALL skip probing and be encoded with the default CQ. The skip SHALL be visible in the plan — via the collapsed line's reason when the whole batch skips, or via the row's skip note in a mixed batch — and SHALL NOT emit a warning-level diagnostic.

#### Scenario: Very short video

- **WHEN** a pending video is shorter than the probing window budget
- **THEN** no probing runs, the video is encoded with the default CQ, and the plan identifies the video as not probed rather than reporting probing as completed

#### Scenario: All-short batch collapses with a reason

- **WHEN** every pending video is shorter than the probing window budget
- **THEN** the plan's collapsed line names the skip reason (short videos) and the default CQ in effect
