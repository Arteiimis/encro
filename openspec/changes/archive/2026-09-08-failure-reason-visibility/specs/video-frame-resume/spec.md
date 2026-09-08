## MODIFIED Requirements

### Requirement: Segmented encoding for MP4 video tasks

MP4 video encode tasks SHALL be encoded in fixed-duration, keyframe-aligned segments rather than a single ffmpeg pass. Each video frame SHALL be encoded exactly once. Each segment SHALL be independently decodable (starting with a keyframe). After all segments are encoded, the final output file SHALL be assembled by concatenating the segments with a lossless remux (`-c copy`). The concatenation manifest SHALL reference segment files with paths that resolve from the manifest's own directory (or absolute paths), so assembly succeeds regardless of whether the run's output directory was given as a relative or absolute path. The resulting output SHALL use the same codec, CRF, and other configured encoding settings as before.

#### Scenario: Normal encode produces final file

- **WHEN** a video encode task completes all its segments
- **THEN** the segments are concatenated into the final output file with a lossless remux
- **AND** the output file is a valid MP4 with the configured codec (e.g., HEVC)

#### Scenario: Relative output directory assembles successfully

- **WHEN** a run encodes with a relative output path (`-o out`) and all segments complete
- **THEN** the final output file is assembled without a manifest resolution error
- **AND** the output file is a valid MP4

#### Scenario: Segment starts with keyframe

- **WHEN** ffmpeg encodes a segment starting at time T
- **THEN** the segment's first video frame is a keyframe at time T
- **AND** no frame is encoded twice across segment boundaries

#### Scenario: Non-video tasks unaffected

- **WHEN** the task is not an MP4 video encode (e.g., WebP/picture encoding, archive packing)
- **THEN** it uses the existing single-pass behavior with no segmentation
