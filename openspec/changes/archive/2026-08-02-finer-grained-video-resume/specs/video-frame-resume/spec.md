## Purpose

Allows MP4 video encode tasks to resume from the segment where encoding was interrupted, instead of restarting the entire file from scratch, by encoding in keyframe-aligned segments and persisting per-segment progress.

## ADDED Requirements

### Requirement: Segmented encoding for MP4 video tasks

MP4 video encode tasks SHALL be encoded in fixed-duration, keyframe-aligned segments rather than a single ffmpeg pass. Each video frame SHALL be encoded exactly once. Each segment SHALL be independently decodable (starting with a keyframe). After all segments are encoded, the final output file SHALL be assembled by concatenating the segments with a lossless remux (`-c copy`). The resulting output SHALL use the same codec, CRF, and other configured encoding settings as before.

#### Scenario: Normal encode produces final file
- **WHEN** a video encode task completes all its segments
- **THEN** the segments are concatenated into the final output file with a lossless remux
- **AND** the output file is a valid MP4 with the configured codec (e.g., HEVC)

#### Scenario: Segment starts with keyframe
- **WHEN** ffmpeg encodes a segment starting at time T
- **THEN** the segment's first video frame is a keyframe at time T
- **AND** no frame is encoded twice across segment boundaries

#### Scenario: Non-video tasks unaffected
- **WHEN** the task is not an MP4 video encode (e.g., WebP/picture encoding, archive packing)
- **THEN** it uses the existing single-pass behavior with no segmentation

### Requirement: Audio handled once outside segments

The audio track of a video task SHALL be processed outside the segmented encode: extracted once before segmenting (losslessly copied when compatible with the output container, otherwise encoded once), and muxed back into the final output during assembly. Segments SHALL NOT contain audio. The final output's audio SHALL be the single extracted track on the original timeline, so audio is never re-encoded per segment.

#### Scenario: Lossless audio copy
- **WHEN** the source audio is compatible with the output container
- **THEN** it is copied without re-encoding and the final output contains the identical audio track

#### Scenario: Incompatible audio is encoded once
- **WHEN** the source audio cannot be copied losslessly into the output container
- **THEN** it is encoded once (not per segment) and the final output contains that single encoded track

#### Scenario: Input without audio
- **WHEN** the input video has no audio track
- **THEN** no audio extraction happens and the final output has no audio track

#### Scenario: Audio temp file missing on resume
- **WHEN** a resumed task's extracted audio temp file is missing from disk
- **THEN** the audio is re-extracted before assembly

#### Scenario: Audio stays in sync
- **WHEN** the final output has been assembled
- **THEN** the audio track spans the original timeline and stays in sync with the concatenated video

### Requirement: Segment progress persistence

The job state SHALL record, for each segmented video task, the number of completed segments and the cumulative encoded duration of those segments. This information SHALL be written to the state file at every segment boundary (forced flush), so a crash loses at most the current in-flight segment.

#### Scenario: Segment completion is persisted
- **WHEN** a segment finishes encoding successfully
- **THEN** the task's completed-segment count and cumulative encoded duration are updated and force-flushed to the state file

#### Scenario: Crash mid-segment
- **WHEN** the process is interrupted while a segment is being encoded
- **THEN** the state file still reflects the segments completed before that one

### Requirement: Resume from first uncompleted segment

When a video encode task is resumed, the encoder SHALL continue at the first uncompleted segment (the point where the previous run stopped), re-encoding only the tail segment that was in flight and any segments after it. Completed segments SHALL NOT be re-encoded.

#### Scenario: Resume after interruption
- **WHEN** a task with N recorded completed segments is resumed
- **THEN** encoding starts at segment N (the segment whose start time is the recorded cumulative encoded duration)
- **AND** segments 0..N-1 are not re-encoded

#### Scenario: In-flight segment is re-encoded
- **WHEN** the previous run was interrupted mid-segment
- **THEN** that segment is re-encoded from its start time

#### Scenario: Missing temp segment files
- **WHEN** a recorded completed segment's temp file no longer exists on disk (e.g., cleaned up externally)
- **THEN** encoding restarts from the first missing segment

### Requirement: Segment-based restore decisions

Restore status for a segmented video task SHALL be determined from its segment records, not from output-file existence alone. A task with incomplete segments SHALL NOT be marked Succeeded merely because a partial output file exists. When all segments exist but the final output is missing, the task SHALL only re-run the concat step.

#### Scenario: Partial output file does not count as success
- **WHEN** a resumed task has recorded segments but its final output file is incomplete or missing
- **THEN** the task is not marked Succeeded and encoding continues from the first uncompleted segment

#### Scenario: Concat-only resume
- **WHEN** all segments exist on disk but the final output file is missing (interrupted during concat)
- **THEN** the task only re-runs the lossless concat step without re-encoding any segment

#### Scenario: Old state without segment records
- **WHEN** a task from a pre-existing state file has no segment records
- **THEN** it uses the existing file-level resume behavior (re-encode from scratch if not Succeeded)

### Requirement: Segment-aware progress reporting

The reported progress percentage for a segmented video task SHALL account for frames encoded in completed segments plus the current segment, so the bar reflects whole-file progress rather than current-segment progress.

#### Scenario: Progress includes completed segments
- **WHEN** the current segment is being encoded after N completed segments
- **THEN** the reported progress is (frames in completed segments + frames in current segment) / total frames

### Requirement: Temp segment lifecycle

Temp segment files SHALL survive interruption so they can be reused on resume. They SHALL be deleted after the final output is assembled successfully. A `--restart` run SHALL discard segment records and clean up stale segment files before starting fresh.

#### Scenario: Segments cleaned after success
- **WHEN** the final output file has been assembled
- **THEN** the temp segment directory is removed

#### Scenario: Segments kept on interruption
- **WHEN** encoding is interrupted before the final output is assembled
- **THEN** the temp segment files remain on disk for the next resume

#### Scenario: Restart discards segments
- **WHEN** a `--restart` run starts for a task with existing segment files
- **THEN** the segment records are reset and stale segment files are removed before encoding
