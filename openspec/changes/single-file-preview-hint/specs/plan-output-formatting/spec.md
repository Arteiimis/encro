## MODIFIED Requirements

### Requirement: Post-encode summary uses the same formatting language

The post-encode summary SHALL render conditionally. When every encode succeeded, it SHALL be the count line carrying the encoded count and the output location (`Encoded 2/2 videos → out`), followed by the preview hint only when the summary's encode results carry exactly one successful video. When any encode failed, it SHALL print the count line with the failing count, followed by the failed-file list, the attention block, and the preview hint, each only when it applies. The attention block is defined by `console-output-conventions`: one severity-marked announcement naming the item count, then its items indented beneath it. Counts, lists, and any ratios SHALL keep the same alignment and signed-percentage conventions as the probe plan table; the `All encoding tasks completed.` and `Summary:` header lines SHALL NOT print. The preview hint's single-success condition is defined by `video-encode-probing`.

#### Scenario: Summary matches plan style

- **WHEN** an encode run completes
- **THEN** the summary's counts and lists follow the same alignment and percentage conventions as the probe plan, with no new per-file detail rows added

#### Scenario: Full success prints the count line and the preview hint

- **WHEN** an encode run completes with no failures and exactly one encoded video
- **THEN** the summary is the count line plus the one-line preview hint, with no header lines, count table, or failure sections

#### Scenario: Multi-file success prints only the count line

- **WHEN** an encode run completes with no failures and more than one encoded video
- **THEN** the summary is the count line alone, with no preview hint lines, header lines, count table, or failure sections

#### Scenario: Failures print the count and the failed list

- **WHEN** an encode run completes with 1 of 2 files failed
- **THEN** the count line names the failure, the failed file is listed, the single successful video's preview hint prints, and no empty attention block prints

#### Scenario: Attention list appears only when present

- **WHEN** an encode succeeds everywhere but a file could not reach the quality floor
- **THEN** the attention announcement and its item print under the count line, and the preview hint follows only when the results carry exactly one successful video
