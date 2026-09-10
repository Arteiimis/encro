## MODIFIED Requirements

### Requirement: Segmented encoding for MP4 video tasks

MP4 video encode tasks SHALL be encoded in fixed-duration, keyframe-aligned segments produced by a single encoder invocation per task attempt: one invocation reads the input once and writes an ordered series of segment files plus a segment list that records each segment's time range. Segments SHALL be cut at the first keyframe at or after each fixed segment-duration mark (10 s), so a boundary is never earlier than its mark and never more than one frame interval later. Each video frame SHALL be encoded exactly once. Each segment SHALL be independently decodable (starting with a keyframe at its boundary). After all segments are encoded, the final output file SHALL be assembled by concatenating the segments with a lossless remux (`-c copy`). The concatenation manifest SHALL reference segment files with paths that resolve from the manifest's own directory (or absolute paths), so assembly succeeds regardless of whether the run's output directory was given as a relative or absolute path. The resulting output SHALL use the same codec, CRF, and other configured encoding settings as before.

#### Scenario: Normal encode produces final file

- **WHEN** a video encode task completes all its segments
- **THEN** the segments are concatenated into the final output file with a lossless remux
- **AND** the output file is a valid MP4 with the configured codec (e.g., HEVC)

#### Scenario: Relative output directory assembles successfully

- **WHEN** a run encodes with a relative output path (`-o out`) and all segments complete
- **THEN** the final output file is assembled without a manifest resolution error
- **AND** the output file is a valid MP4

#### Scenario: Segment starts with keyframe

- **WHEN** the encoder cuts a segment at a boundary
- **THEN** the segment's first video frame is a keyframe at that boundary
- **AND** no frame is encoded twice across segment boundaries

#### Scenario: One encoder invocation per task attempt

- **WHEN** a video task produces any number of segments
- **THEN** the video stream is read and encoded by a single encoder invocation for the whole task attempt
- **AND** no encoder invocation is started per segment

#### Scenario: Segment boundary follows the segment mark

- **WHEN** the encoder reaches a segment-duration mark
- **THEN** the segment is cut at the first keyframe at or after that mark
- **AND** the run reports the segments it has produced, so completed segments can be tracked and resumed

#### Scenario: Non-video tasks unaffected

- **WHEN** the task is not an MP4 video encode (e.g., WebP/picture encoding, archive packing)
- **THEN** it uses the existing single-pass behavior with no segmentation

### Requirement: Segment progress persistence

The job state SHALL record, for each segmented video task, the number of completed segments and the cumulative encoded duration of those segments, where that duration is the fixed-duration mark of the first uncompleted segment (completed count x segment duration). A segment SHALL count as completed only once the encoder has finished writing it and closed it. This information SHALL be written to the state file as each segment completes (forced flush), so a crash loses at most the current in-flight segment.

#### Scenario: Segment completion is persisted

- **WHEN** a segment finishes encoding successfully
- **THEN** the task's completed-segment count and cumulative encoded duration are updated and force-flushed to the state file

#### Scenario: Crash mid-segment

- **WHEN** the process is interrupted while a segment is being encoded
- **THEN** the state file still reflects the segments completed before that one
- **AND** the partially written in-flight segment does not count as completed

#### Scenario: Recorded segments match the encoder's output

- **WHEN** a run records completed segments for a task
- **THEN** the recorded count matches the segments the encoder has finished writing for that run
- **AND** each recorded segment's resume time is its fixed-duration mark (segment index x segment duration)

### Requirement: Resume from first uncompleted segment

When a video encode task is resumed, the encoder SHALL continue in a single invocation from the first uncompleted segment (the point where the previous run stopped), re-encoding only the tail segment that was in flight and any segments after it, and SHALL continue the segment numbering after the last completed segment. Completed segments SHALL NOT be re-encoded. Resume SHALL start at the fixed-duration mark of the first uncompleted segment (segment index x segment duration). Because the encoder cuts at the first keyframe at or after each mark, continuing from that mark starts exactly at the next unencoded frame, so no frame is lost or duplicated across the interruption.

#### Scenario: Resume after interruption

- **WHEN** a task with N recorded completed segments is resumed
- **THEN** encoding starts at segment N (its fixed-duration mark, N x the segment duration)
- **AND** segments 0..N-1 are not re-encoded
- **AND** newly written segments continue the numbering from N

#### Scenario: In-flight segment is re-encoded

- **WHEN** the previous run was interrupted mid-segment
- **THEN** that segment is re-encoded from its start time
- **AND** the partially written file for it is discarded rather than concatenated

#### Scenario: Resumed run reproduces every frame

- **WHEN** a run is interrupted and later resumed to completion
- **THEN** the assembled output contains every source frame exactly once, matching a run that was never interrupted

#### Scenario: Missing temp segment files

- **WHEN** a recorded completed segment's temp file no longer exists on disk (e.g., cleaned up externally)
- **THEN** encoding restarts from the first missing segment
