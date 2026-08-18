## ADDED Requirements

### Requirement: Probe results persist across runs

The system SHALL reuse a previously measured probe decision for an input file when the input file and all decision-affecting settings are unchanged, instead of re-measuring it. A reused decision SHALL be marked as cached in the encoding plan. When the input file or any decision-affecting setting changes, the cached decision SHALL be discarded and probing SHALL re-run for that file.

#### Scenario: Unchanged batch re-run
- **WHEN** the user runs an encode on a batch of videos that were probed in an earlier run, with the same settings
- **THEN** probing is skipped for the unchanged files and their plan lines show the cached CQ marked as cached

#### Scenario: Input file modified
- **WHEN** a previously probed input file has changed (different size or modification time) since it was probed
- **THEN** its cached decision is discarded and the file is probed again

#### Scenario: Decision settings changed
- **WHEN** the user changes a decision-affecting setting (quality floor, codec, encoder preset, or quality metric) between runs
- **THEN** cached decisions from the earlier configuration are not reused and the files are probed again

#### Scenario: Cached decision participates in all plan paths
- **WHEN** a file's decision is reused from the cache during a `--dry-run` or `--yes` run
- **THEN** the plan prints the cached decision marked as cached, and the run otherwise behaves identically to a freshly probed run

#### Scenario: Files without a measurement are never cached
- **WHEN** a pending video is not actually measured (too short to probe, or probing fails for it) and is encoded with the default CQ
- **THEN** no cache entry is created for it and the plan never marks it as cached

#### Scenario: Probing bypass paths do not touch the cache
- **WHEN** the user passes `--crf` (probing is skipped entirely) or a video is dropped before probing because it is already HEVC-encoded
- **THEN** no cache entry is read or written for those videos

#### Scenario: Resume after interruption with a cache hit
- **WHEN** a run is interrupted during encoding, then resumed, and the cached decision for a file matches its re-probe key
- **THEN** the resumed run uses the same CQ as the original run (cache hit and re-probe agree)
