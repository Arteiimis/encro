## ADDED Requirements

### Requirement: Explicit output paths are honored as given

An explicit `--output` path SHALL be used exactly as given: a path with a directory component is created (including missing parents) and written to, and a bare filename (no directory component) resolves against the current working directory without creating any directory. Output-directory preparation SHALL NOT fail on an empty parent path, and a path that would overwrite an input file keeps its existing overwrite guard error.

#### Scenario: Bare filename writes to the working directory

- **WHEN** the user runs `encro preview a.mp4 b.mp4 --output result.mp4`
- **THEN** the comparison video is written to `result.mp4` in the current working directory
- **AND** the run exits successfully without a crash

#### Scenario: Nested directory is created

- **WHEN** the user passes `--output sub/dir/result.mp4` and `sub/dir` does not exist
- **THEN** the directories are created and the preview is written there
