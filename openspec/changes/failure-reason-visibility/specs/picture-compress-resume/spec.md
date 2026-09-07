## ADDED Requirements

### Requirement: Temporary outputs keep a recognizable media extension

Temporary files written by the compression producer SHALL carry a final media extension matching the target format (or pass an explicit container flag to the encoder), so the encoder infers the output format from the temp path. The atomic-rename semantics of `Compression outputs are atomic` are unchanged: the temp file is renamed to the final cached name only after the producer exits successfully.

#### Scenario: Compression succeeds with a temp extension in place

- **WHEN** a picture is compressed with `-c` (for example PNG to JPEG at quality 2)
- **THEN** the producer writes to a temp path ending in the target media extension
- **AND** the compression succeeds and the renamed output is a valid image of the target format

#### Scenario: Atomicity is preserved

- **WHEN** the producer is killed mid-write
- **THEN** no final cached output exists for that picture and the run compresses it again later, unchanged from the atomic-output requirement
