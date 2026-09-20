## MODIFIED Requirements

### Requirement: Probe plan renders as an aligned table

The probe plan SHALL render one row per pending file with column headers: file name, chosen CQ, measured p5 score (one decimal), estimated size (auto-scaled to GB/MB), and ratio as a signed percentage. Numeric columns SHALL be right-aligned; the table body SHALL carry no per-line `[info]` badge prefix. The per-file `est. size:` / `ratio:` continuation lines of the current format SHALL be removed. The block SHALL open with the probe phase's summary line (`pipeline-narration`) above its opening rule, and the totals line SHALL be separated from the table body by exactly one blank line. The table's header row, file rows and totals line SHALL keep the styling they have today: this change adds no emphasis to any of them. When no pending file carries measured probe data, the plan SHALL instead render as a single line naming the file count, the CQ in effect, and the reason no measurements are shown (for example `2 video(s) to encode at CQ 28 (probing skipped: short videos) in 2s`), without table headers or rule lines — that line is the phase's summary line, so no separate probe summary line prints; rows for files without measurements in a mixed batch SHALL state the skip reason in the row.

#### Scenario: Batch plan shows one row per file

- **WHEN** probing completes for a batch of files
- **THEN** the plan prints a header row and one aligned row per file, with no per-file secondary lines

#### Scenario: Plan block opens with the phase summary line

- **WHEN** probing completes with measurements for the batch
- **THEN** the phase summary line prints above the opening rule, and the rule, title, header row and file rows follow it

#### Scenario: Totals are set off from the table

- **WHEN** the plan prints its totals line
- **THEN** exactly one blank line separates the last table row from that line

#### Scenario: p5 score shows one decimal

- **WHEN** a file's measured p5 is 95.00
- **THEN** the row shows `95.0`, and an abnormal score such as 91.56 keeps its second decimal visible

#### Scenario: Batch without measurements collapses to one line

- **WHEN** every pending video skips probing (for example all shorter than the probe window)
- **THEN** the plan renders exactly one line naming the count, the CQ in effect, the skip reason, and the phase's elapsed time, with no table header, rule lines, per-file rows, or separate probe summary line

#### Scenario: Mixed batch keeps the table with skip reasons

- **WHEN** a batch contains probed files and files that skipped probing
- **THEN** the table renders one row per file, and each unprobed row states why it carries no measurements

### Requirement: Plan adapts to terminal width

The table SHALL size its file-name column to the smallest of: the detected terminal width minus the numeric columns' width, the longest file name in the batch, and the width that keeps a row within 85 display columns. The name column SHALL be at least 20 display columns wide. Names longer than the column SHALL be truncated mid-string with an ellipsis while the file extension stays visible. When the terminal is too narrow for even the minimum name column, the layout SHALL fall back to a two-line form (name on its own line, metrics indented on the next) so no data is lost.

#### Scenario: Wide terminal shows full names

- **WHEN** the terminal is 120 columns wide and every file name fits the row budget
- **THEN** file names are shown in full

#### Scenario: Long names are capped on a wide terminal

- **WHEN** the terminal is 250 columns wide and one file name is 120 display columns long
- **THEN** no row of the table exceeds 85 display columns, and that file's name is truncated mid-string with its extension preserved

#### Scenario: Narrow terminal truncates names

- **WHEN** the terminal is 80 columns wide
- **THEN** long names are truncated with an ellipsis and the extension preserved, while the numeric columns stay intact

#### Scenario: Very narrow terminal falls back to two lines

- **WHEN** the terminal width cannot fit the minimum name column
- **THEN** each file prints its name on one line and its metrics indented on the following line

### Requirement: Post-encode summary uses the same formatting language

The post-encode summary SHALL be the encode phase's summary line (`pipeline-narration`). When every encode succeeded, it SHALL be the count line carrying the encoded count, the output location, and the phase's elapsed time (`Encoded 2/2 videos → out in 12m:34s`), followed by the preview hint only when the summary's encode results carry exactly one successful video. When any encode failed, the count line SHALL additionally name the failed count, and it SHALL name the skipped count when files were dropped because their estimated size exceeded their source (`Encoded 5/8 videos (2 failed, 1 skipped) → out in 12m:34s`); the count line's total counts every file the run was responsible for, including those skipped before encoding, so the classes always add up to it. The failed-file list, the attention block, and the preview hint follow, each only when it applies. The summary line SHALL print before the packing phase runs, so a packed run's last product line is the packing phase's own summary line. The attention block is defined by `console-output-conventions`: one severity-marked announcement naming the item count, then its items indented beneath it. Counts, lists, and any ratios SHALL keep the same alignment and signed-percentage conventions as the probe plan table; the `All encoding tasks completed.` and `Summary:` header lines SHALL NOT print. The preview hint's single-success condition is defined by `video-encode-probing`.

#### Scenario: Summary matches plan style

- **WHEN** an encode run completes
- **THEN** the summary's counts and lists follow the same alignment and percentage conventions as the probe plan, with no new per-file detail rows added

#### Scenario: Full success prints the count line and the preview hint

- **WHEN** an encode run completes with no failures and exactly one encoded video
- **THEN** the summary is the count line — counts, destination, elapsed time — plus the one-line preview hint, with no header lines, count table, or failure sections

#### Scenario: Multi-file success prints only the count line

- **WHEN** an encode run completes with no failures and more than one encoded video
- **THEN** the summary is the count line alone, with no class segments, preview hint lines, header lines, count table, or failure sections

#### Scenario: Failures print the count and the failed list

- **WHEN** an encode run completes with 1 of 2 files failed
- **THEN** the count line names the failure in a `(1 failed)` segment, the failed file is listed, the single successful video's preview hint prints, and no empty attention block prints

#### Scenario: Skipped files are counted in the line

- **WHEN** an encode run drops a file because its estimated size exceeds its source size
- **THEN** the count line carries a `(1 skipped)` segment next to the counts, and the plan table's row for that file keeps stating the reason

#### Scenario: Encode summary precedes packing

- **WHEN** a run encodes videos and then packs them
- **THEN** stdout prints the encode summary line before the packing phase's announcement and summary lines

#### Scenario: Attention list appears only when present

- **WHEN** an encode succeeds everywhere but a file could not reach the quality floor
- **THEN** the attention announcement and its item print under the count line, and the preview hint follows only when the results carry exactly one successful video
