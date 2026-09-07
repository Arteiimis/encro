## MODIFIED Requirements

### Requirement: Probe plan renders as an aligned table

The probe plan SHALL render one row per pending file with column headers: file name, chosen CQ, measured p5 score (one decimal), estimated size (auto-scaled to GB/MB), and ratio as a signed percentage. Numeric columns SHALL be right-aligned; the table body SHALL carry no per-line `[info]` badge prefix. The per-file `est. size:` / `ratio:` continuation lines of the current format SHALL be removed. When no pending file carries measured probe data, the plan SHALL instead render as a single line naming the file count, the CQ in effect, and the reason no measurements are shown (for example `2 video(s) to encode at CQ 28 (probing skipped: short videos)`), without table headers or rule lines; rows for files without measurements in a mixed batch SHALL state the skip reason in the row.

#### Scenario: Batch plan shows one row per file

- **WHEN** probing completes for a batch of files
- **THEN** the plan prints a header row and one aligned row per file, with no per-file secondary lines

#### Scenario: p5 score shows one decimal

- **WHEN** a file's measured p5 is 95.00
- **THEN** the row shows `95.0`, and an abnormal score such as 91.56 keeps its second decimal visible

#### Scenario: Batch without measurements collapses to one line

- **WHEN** every pending video skips probing (for example all shorter than the probe window)
- **THEN** the plan renders one line naming the count, the CQ in effect, and the skip reason, with no table header, rule lines, or per-file rows

#### Scenario: Mixed batch keeps the table with skip reasons

- **WHEN** a batch contains probed files and files that skipped probing
- **THEN** the table renders one row per file, and each unprobed row states why it carries no measurements

### Requirement: Post-encode summary uses the same formatting language

The post-encode summary SHALL render conditionally. When every encode succeeded, it SHALL be the count line carrying the encoded count and the output location (`Encoded 2/2 videos → out`) followed by the existing one-line preview hint. When any encode failed, it SHALL print the count line with the failing count, followed by the failed-file list, the existing "Needs attention" list, and `Compare:` hint lines, each only when it applies. Counts, lists, and any ratios SHALL keep the same alignment and signed-percentage conventions as the probe plan table; the `All encoding tasks completed.` and `Summary:` header lines SHALL NOT print.

#### Scenario: Summary matches plan style

- **WHEN** an encode run completes
- **THEN** the summary's counts and lists follow the same alignment and percentage conventions as the probe plan, with no new per-file detail rows added

#### Scenario: Full success prints the count line and the preview hint

- **WHEN** an encode run completes with no failures
- **THEN** the summary is the count line plus the one-line preview hint, with no header lines, count table, or failure sections

#### Scenario: Failures print the count and the failed list

- **WHEN** an encode run completes with 1 of 2 files failed
- **THEN** the count line names the failure, the failed file is listed, and no empty "Needs attention" section prints

#### Scenario: Attention list appears only when present

- **WHEN** an encode succeeds everywhere but a file could not reach the quality floor
- **THEN** the "Needs attention" entry prints under the count line, with the preview `Compare:` hint attached
