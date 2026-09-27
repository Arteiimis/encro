## ADDED Requirements

### Requirement: Copy failures are named in the report

The end-of-run report SHALL list every image whose copy failed, naming the
source path and the destination path, after the run totals and in the report's
indented detail-line style. The run SHALL report what was actually copied in its
totals, and a failed copy SHALL NOT change the run's exit code: the exit code
SHALL be the one the run would end with otherwise (0 for a completed run), and a
re-run SHALL retry exactly the copies that are missing.

#### Scenario: A failed copy is named in the report

- **WHEN** a run completes with an image whose copy failed (for example the
  destination tree cannot be written)
- **THEN** the report lists that image's source and destination after the totals
  line
- **AND** the run exits with the exit code it would otherwise have (0)
