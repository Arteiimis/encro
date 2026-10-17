# Spec Delta

## ADDED Requirements

### Requirement: A scan that drops already-encoded videos says so

A video scan that leaves files out because they are already in the target codec SHALL print one hint line naming the count and the reason, on stderr, so the console count reconciles with the directory's file count. The per-file names SHALL stay in the log, keeping the console at one line for this filter.

#### Scenario: A directory scan names the skipped count

- **WHEN** a directory scan for MP4 output finds five videos of which two are already HEVC
- **THEN** one hint line states that two files were skipped because they are already HEVC
- **AND** the encode phase proceeds with the remaining three

#### Scenario: Nothing skipped prints no line

- **WHEN** every scanned video needs encoding
- **THEN** no skipped-file hint prints
