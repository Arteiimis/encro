# picture-video-webp Specification

## Purpose

Converts the short video files mixed into a picture set to animated WebP inside the picture workflow, so one run packs pictures and clips together under one naming scheme.

## Requirements

### Requirement: Video conversion flag surface

Picture runs SHALL accept a `--video-webp` flag that enables converting the videos found under the input. The flag SHALL be independent of picture compression: with `-c`, without `-c`, and together with `-c` are all valid. Passing the flag to a run whose process type is not `picture` SHALL fail with an error naming the flag and the required process type, rather than ignoring it.

#### Scenario: Flag enables conversion in a picture run

- **WHEN** a picture run over a directory holding pictures and videos is started with `--video-webp`
- **THEN** the videos are converted and their results are packed together with the pictures

#### Scenario: Conversion without picture compression

- **WHEN** a picture run is started with `--video-webp` and without `-c/--compress`
- **THEN** the videos are converted and packed, and the pictures are packed without being compressed

#### Scenario: Flag rejected outside picture runs

- **WHEN** `--video-webp` is passed to a run whose process type is `video`
- **THEN** the run fails with an error stating that the flag is only supported with `--type picture`

### Requirement: Videos are scanned by the video extension set

With the flag enabled, the run SHALL scan the input for the video file extensions the video workflow already recognizes (`.mp4`, `.mkv`, `.avi`, `.mov`, `.flv`, `.wmv`), honoring the run's recursion setting. A scanned video SHALL NOT be excluded for its source codec: a clip that is already HEVC encoded is still converted. Inputs that exceed the WebP input size limit the video workflow applies (32 MB) SHALL be skipped with a warning and SHALL NOT be packed.

#### Scenario: Already-HEVC clip is still converted

- **WHEN** the input holds pictures and one HEVC-encoded video, and the run enables conversion
- **THEN** that video is converted and its entry is packed

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

### Requirement: Conversion uses the video WebP recipe

A conversion SHALL use the same encoder recipe as the video workflow's WebP output: animated WebP (all frames, looping), the height cap and quality search that recipe defines, audio dropped. The same input converted through a picture run and through a video run SHALL be encoded with identical parameters.

#### Scenario: Recipe matches the video WebP path

- **WHEN** the same clip is converted by a picture run with `--video-webp` and by a video run with `-f webp`
- **THEN** both invocations request the same encoder, filter chain, loop setting and starting quality
- **AND** both apply the same adaptive quality search against the same size target

#### Scenario: Conversion output is an animated WebP

- **WHEN** a clip with several frames is converted
- **THEN** the produced file is a WebP carrying those frames and looping

### Requirement: Converted videos join the picture entry naming scheme

A converted video SHALL be packed under the naming scheme the picture entries of the same run use — the flat picture prefix with its source stem and the `.webp` extension under the default layout, the clip's path relative to the input under the keep layout — and SHALL follow the same conflict-handling rules, so no entry clobbers another. Its grouping SHALL come from the clip's own source directory, so a converted clip stays in the archive that holds the pictures of that directory and never lands in an archive of its own.

#### Scenario: Video entry sits with the picture entries

- **WHEN** a run with conversion packs an input holding pictures and a video with stem `clip`
- **THEN** the archive holds the picture entries and an entry named `clip` with the `.webp` extension under the same naming prefix the pictures use
- **AND** that entry is grouped with the pictures of the clip's own source directory

#### Scenario: Keep layout is honored

- **WHEN** a run with conversion packs with the keep layout and the clip lives in a subdirectory
- **THEN** the clip's entry name is its path relative to the input with the `.webp` extension, matching how the pictures of that directory are named

#### Scenario: Name conflicts are disambiguated like pictures

- **WHEN** two videos with the same stem are converted into one flat pack
- **THEN** their entries are made distinct by the same conflict-handling rules the picture entries use
- **AND** both converted files are present in the archive

### Requirement: The converted output replaces its source clip

For every successfully converted video the packing step SHALL pack the converted WebP and SHALL NOT pack the source clip, regardless of whether the converted file is larger than the source.

#### Scenario: Converted output is larger than the source

- **WHEN** a video converts to a WebP that is larger than the source file
- **THEN** the converted WebP is packed and the source clip is not

#### Scenario: Failed conversion leaves no entry

- **WHEN** a video's conversion fails after the encoder's attempts are exhausted
- **THEN** neither a converted output nor the source clip is packed for that video
- **AND** the run reports the failure

#### Scenario: Every conversion fails

- **WHEN** every video in the input fails to convert
- **THEN** the run fails with an error and does not pack an archive without the requested conversions

### Requirement: Conversion cache lives in the hidden work directory

Converted outputs SHALL be written to a conversion cache inside the run's hidden work directory, separate from the picture compression cache and not keyed by the picture compression quality. The cache SHALL be preserved when a run is canceled or interrupted after conversion started, and SHALL be removed when a run completes successfully. A run with `--restart`, or a run whose saved state does not match the current command, SHALL start from an empty conversion cache.

#### Scenario: Successful run cleans up the conversion cache

- **WHEN** a run with conversion completes successfully
- **THEN** the conversion cache directory is removed

#### Scenario: Canceled run preserves the conversion cache

- **WHEN** a run is canceled while videos are being converted
- **THEN** the conversion cache and the outputs already produced remain on disk
- **AND** a later run of the same command can resume from them

#### Scenario: Picture quality change does not invalidate the conversion cache

- **WHEN** a run with conversion is followed by a run of the same input that changes `-q/--image-quality`
- **THEN** the conversion cache is not treated as another run's cache
- **AND** videos already converted are not converted again

#### Scenario: Restart discards the conversion cache

- **WHEN** a run with conversion is started with `--restart`
- **THEN** the conversion cache is removed and every video is converted again

### Requirement: Conversion resume is decided per video

A resumed run SHALL convert only the videos that have no valid converted output, and SHALL pack the converted outputs that already exist. A converted output SHALL be written through a temporary file and renamed to its final cached name only after the encoder exited successfully, so a file at a final converted path is always complete. A cached output SHALL be reused only while the source clip it was produced from is unchanged: a clip whose source file was modified or replaced after its conversion SHALL be converted again, and its stale cached output SHALL be discarded rather than restored or packed.

#### Scenario: Partial run resumes conversion

- **WHEN** a run was canceled after converting some videos and the same command runs again
- **THEN** only the videos without a valid converted output are converted
- **AND** the previously converted videos are packed from the conversion cache

#### Scenario: Interrupted conversion is redone

- **WHEN** the conversion of a video was killed while writing its output
- **THEN** that video is converted again on the next run
- **AND** the partial output is never packed

#### Scenario: A final converted path only ever holds a complete output

- **WHEN** a conversion is interrupted before the encoder exits successfully
- **THEN** the final cached path for that video does not hold an output
- **AND** what the encoder wrote stays under a temporary name carrying a WebP extension

#### Scenario: Replaced source clip is converted again

- **WHEN** a video was converted, its source file was then replaced (same path, new content), and the run resumes
- **THEN** the stale cached output is discarded and the video is converted again
- **AND** the archive is produced from the fresh output, not from the stale one

#### Scenario: Missing state invalidates the conversion cache

- **WHEN** a conversion cache exists but no saved state matches the current command
- **THEN** the cache is not trusted and every video is converted again

### Requirement: Conversion runs as its own reported phase

The conversion SHALL run after picture compression and before packing, and packing SHALL start only after every conversion has settled. The phase SHALL announce itself once with a line naming the count of videos to convert, and SHALL report its own progress while it runs, separate from the picture compression progress. When the input holds no videos, no conversion phase SHALL run.

#### Scenario: Phase order within one run

- **WHEN** a picture run with compression and conversion packs successfully
- **THEN** the output shows a picture compression phase, then a video conversion phase, then packing

#### Scenario: Packing waits for conversions

- **WHEN** conversions are still running
- **THEN** no archive is created until every conversion has settled

#### Scenario: No videos means no conversion phase

- **WHEN** a run enables conversion over an input that holds no videos
- **THEN** no conversion phase runs and the run packs the pictures as before

### Requirement: Conversion concurrency is capped independently

The conversion phase SHALL run at most two conversions at a time, further limited by `-j/--jobs` when that is smaller, so a conversion never takes a picture compression worker slot and parallel conversions never oversubscribe the machine.

#### Scenario: Picture compression is not starved by conversions

- **WHEN** a run compresses pictures and converts several videos
- **THEN** the tool's invocation log shows no more than two conversions in flight at once

#### Scenario: Requested job count is respected

- **WHEN** a run sets `-j/--jobs 1` together with conversion
- **THEN** no more than one conversion runs at a time
