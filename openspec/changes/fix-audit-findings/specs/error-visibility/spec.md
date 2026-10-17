# Spec Delta

## ADDED Requirements

### Requirement: Every directory walk tolerates unreadable entries

Every directory walk in the program besides the media scanner — the directory packer's file collection, organize's disposition pre-pass and reference sampling, batch subfolder discovery, the picture cache sweep, and the ffmpeg/ffprobe install-directory search — SHALL report an entry it cannot read and continue with the rest, and SHALL report a file that disappears between listing and use, instead of aborting the run with an uncaught filesystem error.

#### Scenario: The packer's walk tolerates an unreadable subdirectory

- **WHEN** a directory pack walks a tree containing a subdirectory it cannot read
- **THEN** the pack continues with the entries it can read
- **AND** a warning-level log record names the unreadable subdirectory

#### Scenario: A file that vanishes mid-operation is reported

- **WHEN** a file listed for an operation is deleted before its size is read
- **THEN** the run reports which file could not be read and does not abort with an unhandled exception

#### Scenario: Organize's pre-pass tolerates an unreadable folder

- **WHEN** organize's disposition pre-pass meets a first-level entry it cannot inspect
- **THEN** the run names that entry and continues with the folders it could read
