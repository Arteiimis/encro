# Spec Delta

## ADDED Requirements

### Requirement: An existing model file that fails its pin is repaired

A model file that exists but does not match its pinned size SHALL count as missing: the run SHALL name it among the missing files, and `--download-models` SHALL download it again and verify it. A file that fails its pin SHALL never be accepted as installed, and a re-run SHALL converge on the pinned content instead of reporting the install complete.

#### Scenario: A wrong-sized model file is downloaded again

- **WHEN** the model directory holds `model.onnx` with a size that does not match the pin and `--download-models` runs
- **THEN** the file is downloaded again, verified against its pinned checksum, and replaced

#### Scenario: A wrong-sized model file is named as missing

- **WHEN** organize runs without `--download-models` and a model file has the wrong size
- **THEN** it exits non-zero before scanning and names that file as missing

#### Scenario: Repair is idempotent

- **WHEN** `--download-models` runs again after a successful repair
- **THEN** nothing is downloaded

### Requirement: A runtime extraction completes or does not exist

Installing the cuDNN runtime SHALL stage each extracted DLL and confirm the write completed before the file becomes visible under its final name. The runtime SHALL count as installed only when every extracted DLL is in place: the file the install check tests SHALL be moved into place last, so an interrupted extraction leaves the runtime reported as missing and the next run extracts again.

#### Scenario: An interrupted extraction is not an install

- **WHEN** extraction stops before every DLL has been written
- **THEN** the DLL the install check tests is absent and the next run extracts the archive again

#### Scenario: A complete extraction is an install

- **WHEN** every DLL lands under its final name
- **THEN** the install check reports the runtime installed and later runs skip the extraction

#### Scenario: A failed DLL write fails the install

- **WHEN** a DLL cannot be written
- **THEN** the install reports an error and the runtime is not reported as installed

## MODIFIED Requirements

### Requirement: Dry run

`--dry-run` SHALL run the full analysis and print the assignment plan (the report) without copying anything and without creating output folders beyond the cache. The report's copy and skip counters SHALL state what a real run would do: an image whose destination already holds identical content counts as skipped, every other image counts as one to copy, and the totals line SHALL word those counts as conditional.

#### Scenario: Dry run copies nothing

- **WHEN** `encro organize <dir> --dry-run` runs
- **THEN** the report is printed, no `organized/<name>/` folders are created, and no images are copied

#### Scenario: Dry run reports what it would do

- **WHEN** a dry run analyses a folder where one image's destination already holds identical content and another image has no destination yet
- **THEN** the report counts one image as skipped and one as a would-copy
