# Spec Delta

## ADDED Requirements

### Requirement: A directory pack never packs the program's own work directory

A pack of a directory SHALL exclude encro's own work directory: no `.encro` entry at the packed root or below it SHALL reach the archive, so an archive never carries job state, resume segments or picture caches. Other dot-prefixed entries SHALL remain eligible, so packing a user tree keeps its hidden configuration files.

#### Scenario: The work directory is excluded

- **WHEN** `--pack-only` packs a root that holds `.encro/job-state.json` and `.encro/segments/seg_0.ts`
- **THEN** the archive contains no entry under `.encro`

#### Scenario: Other hidden entries are still packed

- **WHEN** the packed root holds `.config/app.json`
- **THEN** that entry is present in the archive

#### Scenario: A nested work directory is excluded too

- **WHEN** a subdirectory of the packed root holds its own `.encro` directory
- **THEN** no entry under that `.encro` reaches the archive
