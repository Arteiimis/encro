# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.3.0] - 2026-04-30

No user-visible changes: this milestone moved the packing subsystem onto an object-oriented API and deleted the deprecated façade in front of it.
## [1.2.0] - 2026-04-29

### Fixed

- The pack progress bar no longer jumps between parallel archives.
- A finalizing indicator now appears while an archive is being closed, and stops flashing once it completes.
- The completion hook no longer overwrites the packing text while an archive finalizes.
## [1.1.0] - 2026-04-27

### Changed

- Packing in compact progress mode reports its own progress and status instead of going silent until it finishes.
## [1.0.0] - 2026-04-26

### Added

- Compact progress mode, which shows one bar per stage instead of one per file; `--full-progress` brings back the per-worker and per-archive bars.
- Progress bars are coloured by outcome, so finished, failed and active work differ at a glance.

### Changed

- Video encoding runs faster on the same hardware.
- Per-archive success lines were removed from compact mode, where the progress bars already say the same thing.
## [0.4.0] - 2026-04-19

### Added

- Packing is resumable: an interrupted zip is continued rather than restarted.
- `Ctrl-C` requests a stop and lets running work finish cleanly before exiting.
- Directory packing builds its plan from the directory itself, including subparts, instead of requiring a caller to enumerate files.
- An invalid command line prints a short hint pointing at `-h`.

### Changed

- Video info and encoding state are cached, so repeated work over the same files skips the expensive steps.
- MP4 output names account for whether the file will be packed.

### Fixed

- Name collisions across the video and pack paths resolve through one shared set of naming rules instead of two divergent ones.
## [0.3.0] - 2026-04-13

### Added

- Output layout options, with `-o` accepting `+` / `input://` and `=` / `common://` aliases for the input root and the common root.
- `--force-conflict-handling` decides what happens to same-name files in a flat output directory.
- Zips are grouped by size and file count, and files from one source directory stay together across a split.
- Sequential ordinal names for split archives, so parts are ordered by name.
- The help header names the build it came from.

### Fixed

- Duplicate entry names inside a single archive are resolved instead of producing an unreadable zip.
## [0.2.0] - 2026-03-05

### Added

- A crash prints a stack trace and exits with a message instead of disappearing.
- `-j` / `--jobs` sets how many files are processed in parallel.
- `-v` / `--verbose` and `-e` / `--verbose-echo` control how much of the progress is repeated to the terminal.

### Changed

- Process-type aliases (`vid` / `pic`) are accepted alongside the full names.
## [0.1.5] - 2026-03-02

### Added

- Multiple inputs can be passed in one invocation, with validation of what they add up to.

### Changed

- Video files are processed in parallel.
- The packing phase shows progress while it finalizes an archive.
- Command-line mistakes are reported with enough detail to fix them.
## [0.1.0] - 2026-02-27

### Added

- Transcoding video to HEVC via NVENC, one file at a time with a progress bar per file.
- Packing the encoded output into zip archives.
- Command-line options for output format, output directory and recursion.

[unreleased]: https://github.com/Arteiimis/encro/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/Arteiimis/encro/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/Arteiimis/encro/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/Arteiimis/encro/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Arteiimis/encro/compare/v0.4.0...v1.0.0
[0.4.0]: https://github.com/Arteiimis/encro/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Arteiimis/encro/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Arteiimis/encro/compare/v0.1.5...v0.2.0
[0.1.5]: https://github.com/Arteiimis/encro/compare/v0.1.0...v0.1.5
[0.1.0]: https://github.com/Arteiimis/encro/releases/tag/v0.1.0
