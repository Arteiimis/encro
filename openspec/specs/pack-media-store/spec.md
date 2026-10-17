# pack-media-store Specification

## Purpose

The pack phase stores already-compressed media entries (video, audio, and image containers) in the archive without deflate compression, while all other file types keep deflate compression.

## Requirements

### Requirement: Media entries are stored uncompressed

Pack entries whose file extension identifies an already-compressed media container SHALL be written to the archive with the STORE method (no compression) and SHALL remain fully readable in the resulting archive.

#### Scenario: Video files in an archive
- **WHEN** the pack phase archives a batch that includes MP4 video files
- **THEN** the video entries are stored in the archive without deflate and the archive remains fully readable

#### Scenario: Image files in an archive
- **WHEN** the pack phase archives a batch that includes JPEG, WebP, or PNG files
- **THEN** those entries are stored in the archive without deflate

### Requirement: Non-media entries keep deflate

Pack entries that are not recognized as already-compressed media SHALL keep the default deflate compression.

#### Scenario: Mixed batch
- **WHEN** the pack phase archives a batch containing both MP4 files and text files
- **THEN** the text entries are deflated and the media entries are stored without deflate

### Requirement: Media recognition is extension-based

A pack entry SHALL be recognized as already-compressed media by its file extension, matched case-insensitively. The media whitelist SHALL cover common compressed video, audio, and image containers: `.mp4`, `.mkv`, `.mov`, `.avi`, `.webm`, `.flv`, `.wmv`, `.m4v`, `.ts`, `.mpg`, `.mpeg`, `.3gp`, `.m4a`, `.aac`, `.mp3`, `.flac`, `.ogg`, `.opus`, `.wma`, `.ac3`, `.jpg`, `.jpeg`, `.webp`, `.png`, `.gif`, `.heic`, `.avif`. Entries whose extension is not in the whitelist — including uncompressed containers such as `.wav`, `.aiff`, `.bmp`, `.ppm`, `.tif`, `.tiff` — SHALL default to deflate.

#### Scenario: Unknown extension
- **WHEN** the pack phase archives a file whose extension is not in the media whitelist
- **THEN** the entry is deflated like any other non-media file

#### Scenario: Uppercase media extension
- **WHEN** the pack phase archives a file named `video.MP4` or `photo.JPG`
- **THEN** the entry is stored without deflate, matching the case-insensitive rule

#### Scenario: Uncompressed container stays deflated
- **WHEN** the pack phase archives a `.wav` or `.bmp` file
- **THEN** the entry is deflated, because those containers are not already-compressed media

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
