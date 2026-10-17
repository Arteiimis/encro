# Spec Delta

## ADDED Requirements

### Requirement: Video inputs cover the common video containers

The video input set SHALL recognise the common video containers ffmpeg demuxes: `.mp4`, `.mkv`, `.mov`, `.avi`, `.webm`, `.flv`, `.wmv`, `.m4v`, `.ts`, `.m2ts`, `.mpg`, `.mpeg` and `.3gp`. The set SHALL apply identically to a directory scan and to a file named directly on the command line, so naming a file with one of these extensions is never silently ignored.

#### Scenario: A named webm file is a video input

- **WHEN** the input is the single file `clip.webm`
- **THEN** the run treats it as a video input and proceeds to encode it

#### Scenario: A directory scan finds the same containers

- **WHEN** a scanned directory holds a `.webm` clip and a `.mkv` clip
- **THEN** both are video inputs

#### Scenario: Unlisted extensions are not video inputs

- **WHEN** a scanned directory holds a `.srt` subtitle file and a `.txt` file
- **THEN** neither is a video input
