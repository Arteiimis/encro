## ADDED Requirements

### Requirement: Identity model input preparation

The identity feature SHALL be computed from input prepared exactly the way the identity model's reference implementation prepares it: the image resampled to the model's input edge with the reference implementation's resampling filter (aspect ratio not preserved), pixel values scaled to the 0..1 range, and then normalized per channel with the model's documented channel mean and standard deviation, delivered in the model's documented tensor layout. No other step SHALL alter the pixel values handed to the model, and the feature SHALL remain stored as a unit-length vector. The input contract that belongs to the pinned model file (edge size, resampling filter, scaling, normalization, layout) SHALL be stated next to that pin, so that replacing the model file requires restating its contract instead of inheriting the previous model's preparation.

#### Scenario: Input matches the model's reference preprocessing

- **WHEN** a frame with known pixel values passes through the identity input preparation
- **THEN** the frame is resampled to the model's input edge with the reference resampling filter, each channel is scaled to 0..1 and then normalized with that channel's documented mean and standard deviation, and the values reach the model in the documented layout

#### Scenario: The input contract is recorded beside the model pin

- **WHEN** the pinned identity model file and the input contract recorded with it are read together
- **THEN** the record names the input edge, the resampling filter, the pixel scaling, the per-channel normalization and the tensor layout the conversion implements, so a replacement cannot inherit them silently

## MODIFIED Requirements

### Requirement: Cache and resume

Analysis results SHALL be cached keyed by SHA-256 of file content under `<directory>/organized/.cache/`, and SHALL hold everything a later run needs to skip re-analysis: the identity feature and the tag pairs. Re-runs SHALL skip analysis for unchanged images (renames and moves still hit the cache). The cache SHALL be persisted in bounded batches (not one rewrite per image) and flushed at the analysis stage boundary and on interruption, so an interrupted run (including Ctrl-C) resumes without redoing completed analysis beyond the in-flight batch. Stored tag pairs SHALL be limited to each category's consuming threshold (general at or above the naming floor, character at or above the weakest routing threshold) so identity noise cannot dominate the store. A cache written by a different cache format version SHALL be treated as empty, so an upgrade re-analyzes every image once instead of reading entries that lack the identity feature. The format version SHALL be raised whenever the stored analysis changes meaning, not only when the entry shape changes: a model replacement or a change in how the model input is prepared makes every stored feature and tag pair stale, and a cache that mixed entries from two preparations would compare values that are not comparable. `--recluster` SHALL discard cached analysis before running. Cached rating tags SHALL never influence folder assignment.

#### Scenario: Re-run does not re-analyze

- **WHEN** a run completes and the same directory is scanned again with no changes
- **THEN** no model inference runs for the already-cached images

#### Scenario: Interrupted run resumes

- **WHEN** a run is interrupted mid-analysis and re-run
- **THEN** analysis continues from the cache and previously analyzed images are not re-processed

#### Scenario: Cache from an earlier format is discarded

- **WHEN** a cache written by an earlier cache format version is present at the start of a run
- **THEN** the run analyzes every image again, and the file is rewritten as the new entries are stored

#### Scenario: A change in how the model input is prepared invalidates stored features

- **WHEN** a release changes the preparation of the model input, and a cache written by the previous release is present at the start of a run
- **THEN** the run analyzes every image again instead of reading features computed from the previous preparation
