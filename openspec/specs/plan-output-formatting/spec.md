# plan-output-formatting Specification

## Purpose

Defines the terminal output format of the pre-encode probe plan and the post-encode summary: aligned one-row-per-file tables, file-name ordering with warning grouping, signed-percentage ratios, and width-aware layout that stays readable on narrow terminals.

## Requirements

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

### Requirement: Plan rows are sorted with warnings at the bottom

Rows SHALL be ordered by file name (UTF-8 byte order). Files whose quality floor is unreachable SHALL be grouped after all normal rows (same ordering within the group) and SHALL carry a warning marker; a one-line count of such files SHALL be printed before the table.

#### Scenario: Warnings grouped and counted
- **WHEN** 4 of 19 files cannot reach the floor
- **THEN** the plan prints a line noting the 4 files, lists the 15 normal rows in name order, then the 4 warning rows in name order with a warning marker

### Requirement: Ratio is a signed percentage

The plan SHALL express each ratio as a signed percentage (`−20%` for a 0.80 ratio, `+24%` for 1.24), and SHALL mark ratios above 1.0 (estimated output larger than the source) with an explicit indicator.

#### Scenario: Shrinking output shows a negative percentage
- **WHEN** the estimated output is 0.80x the source
- **THEN** the ratio column shows `−20%`

#### Scenario: Growing output is flagged
- **WHEN** the estimated output is 1.24x the source
- **THEN** the ratio column shows `+24%` with an indicator that the output grows

### Requirement: Plan adapts to terminal width

The table SHALL size the file-name column from the detected terminal width minus the numeric columns' width, with a minimum name-column width; names longer than the column SHALL be truncated mid-string preserving the extension. When the terminal is too narrow for even the minimum name column, the layout SHALL fall back to a two-line form (name on its own line, metrics indented on the next) so no data is lost.

#### Scenario: Wide terminal shows full names
- **WHEN** the terminal is 120 columns wide
- **THEN** file names up to the available width are shown in full

#### Scenario: Narrow terminal truncates names
- **WHEN** the terminal is 80 columns wide
- **THEN** long names are truncated with an ellipsis and the extension preserved, while the numeric columns stay intact

#### Scenario: Very narrow terminal falls back to two lines
- **WHEN** the terminal width cannot fit the minimum name column
- **THEN** each file prints its name on one line and its metrics indented on the following line

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

