## MODIFIED Requirements

### Requirement: Videos are scanned by the video extension set

With the flag enabled, the run SHALL scan the input for the video file extensions the video workflow already recognizes (`.mp4`, `.mkv`, `.avi`, `.mov`, `.flv`, `.wmv`), honoring the run's recursion setting. The set is matched case-insensitively (`media-scan`), so uppercase and mixed-case spellings of these extensions are scanned too. A scanned video SHALL NOT be excluded for its source codec: a clip that is already HEVC encoded is still converted. Inputs that exceed the WebP input size limit the video workflow applies (32 MB) SHALL be skipped with a warning and SHALL NOT be packed.

#### Scenario: Already-HEVC clip is still converted

- **WHEN** the input holds pictures and one HEVC-encoded video, and the run enables conversion
- **THEN** that video is converted and its entry is packed

#### Scenario: Uppercase video extension is scanned

- **WHEN** the input holds a clip named `CLIP.MP4` and the run enables conversion
- **THEN** that clip is scanned and converted like its lowercase spelling

#### Scenario: Oversized clip is skipped

- **WHEN** a video in the input exceeds the WebP input size limit
- **THEN** the run warns that the file is skipped for WebP output, does not convert it, and does not pack it

#### Scenario: Recursion applies to the video scan

- **WHEN** a recursive picture run enables conversion and a video lives in a subdirectory
- **THEN** that video is converted and packed

#### Scenario: Video scan reports one line

- **WHEN** a picture run with conversion scans an input for videos with stdout not a terminal
- **THEN** exactly one line names the count of videos found and the input root, in the run's existing scan-narration form

#### Scenario: A clips-only input directory still fails

- **WHEN** a picture run with conversion is pointed at a directory that holds videos but no pictures
- **THEN** the run fails with the existing "no pictures found" error and neither converts nor packs
