## MODIFIED Requirements

### Requirement: Encoding plan is presented before the confirmation prompt

Before the confirmation prompt, the system SHALL print a per-file plan containing, for each pending video: the chosen CQ, the measured p5 quality score rendered in the units of the metric actually used for that file, and the estimated output size. When any file carries measured data, the plan SHALL also print totals (aggregate estimated size and compression ratio); totals SHALL NOT print when no estimates exist. The plan SHALL flag files whose floor is unreachable. When no pending file carries measured probe data, the plan SHALL render as the collapsed single-line form defined by `plan-output-formatting`, which is itself the phase's summary line. Otherwise the plan block SHALL open with the probe phase's summary line defined by `pipeline-narration`, carrying the probed and not-probed counts and the phase's elapsed time. In both forms no other probing-completion line SHALL print.

#### Scenario: Plan precedes the prompt

- **WHEN** probing completes for a batch
- **THEN** a plan listing every pending file with its decision is printed, followed by the existing `(Y/n)` confirmation prompt

#### Scenario: Plan block carries the probe phase summary line

- **WHEN** probing completes for a batch that carries measurements
- **THEN** the probe phase's summary line prints above the plan's opening rule, and no separate probing-completion line prints after the table

#### Scenario: Collapsed plan is the phase summary line

- **WHEN** every pending video skips probing
- **THEN** the collapsed plan line carries the phase's counts and elapsed time, and no separate probe summary line prints

#### Scenario: Plan estimates size

- **WHEN** probing completes for a file
- **THEN** the plan line shows the estimated output size and the compression ratio against the source

#### Scenario: Totals are omitted without estimates

- **WHEN** the batch carries no measured probe data
- **THEN** no totals line prints, and no ratio is computed against an empty estimate

#### Scenario: Mixed-metric batch renders each row in its own units

- **WHEN** a batch contains files scored with different metrics (e.g. one HDR file scored with SSIM and non-HDR files scored with VMAF)
- **THEN** each plan row renders its p5 value formatted for that file's metric instead of forcing one unit onto all rows, and the plan header describes the floor in its stable VMAF-scale meaning rather than deriving its metric name from any single file's metric
