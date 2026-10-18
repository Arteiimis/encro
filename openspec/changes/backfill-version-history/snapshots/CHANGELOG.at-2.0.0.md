# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-10-18

First public release. Everything below this version is reconstructed from the project's own history: these versions were tagged retrospectively and were never distributed, so the pre-release identifiers record milestones rather than published releases.

### Added

- `batch` processes every first-level subfolder of a root directory as its own task, so a season or an artist folder can be handed over in one command.
- Batch runs ask for confirmation once and then apply the answer to every subfolder, instead of prompting per folder.
- A directory scan that skips files by policy — video that is already HEVC — says so in one hint line instead of leaving the reader to wonder why the count is short.

### Changed

- Version numbering, tagging and `CHANGELOG.md` are now part of the project's own contract, and `encro --version` reports the version declared in the build definition rather than a literal in source.
- `organize --dry-run` resolves the same destination decisions as a real run and words its totals as "would copy".
- The encoder settings that change an output — codec, `--crf`, `--preset`, `--min-vmaf` — now take part in job-state matching, so a resumed run can no longer mistake work done with different settings for its own.

### Fixed

- An explicitly named `.webm`, `.m4v`, `.ts`, `.mpg`, `.mpeg`, `.3gp` or `.m2ts` input is treated as video instead of being silently ignored with exit code 0.
- A probe cache that cannot be read in full is discarded and re-probed, instead of aborting the run before the first encode.
- The job-state file is replaced atomically, so a crash can no longer leave it missing.
- Two distinct paths on a case-sensitive filesystem stay two distinct tasks, so a resume can no longer report a never-encoded file as done.
- `--download-models` re-downloads a model file whose size does not match its pin instead of skipping it and reporting success.
- A cuDNN extraction is staged and verified, so an interrupted install cannot look complete.
- An unreadable directory no longer aborts the whole pack, organize pass or toolchain search.
- Packing a directory skips encro's own `.encro` work directory.
- A confirmation prompt whose standard input is closed fails with a message, instead of reporting a user cancellation.

## [2.0.0-rc.1] - 2026-10-05

### Added

- `organize` can group images into an existing folder structure instead of only creating a new one.
- `ENCRO_DEBUG_DUMP_RENDER` writes the exact bytes each stream received, in write order, for diagnosing the terminal layout without a screen.

### Changed

- Identity matching normalizes the input before embedding, so the same character is recognized across differently prepared crops.
- Tag fusion and average linkage make folder grouping order-independent: the same image set produces the same groups regardless of input order.
- A single run's folder set no longer differs from the previous run's when the same images are grouped again.
- The organize spinner and its report now line up with the rest of the output.

## [2.0.0-alpha.3] - 2026-09-28

### Added

- `encro organize` groups a folder of artwork into per-character folders using a local model; image content never leaves the machine.
- `organize --download-models` fetches the model files, and can fetch them without running a grouping pass.
- Shell completion for PowerShell and bash, with `encro completion --install` / `--uninstall` managing the startup wiring.
- `encro config` reads and writes user-level defaults, persisted to `config.json`, so options that never change stop being retyped.
- Failed runs state why they failed instead of only that they did, and the log hint prints on failure rather than on every run.
- Packing can be cancelled inside the archive write, and the partial archive is removed rather than left behind.
- `organize` reports copy failures per file instead of stopping silently.

### Changed

- **Breaking:** a cancelled run now exits with code 130 instead of 1, so a cancellation is distinguishable from a failure.
- **Breaking:** `--download-models` now also needs the identity-embedding model; the total model download grows to roughly 530 MB and help output names both models.
- Progress reporting was unified: bars scroll their labels consistently, compact mode reports probe progress, and pipeline output is less noisy during long runs.
- Help layout follows git's convention, `-h` lists the common flags first, and the colour palette was simplified.
- Media extensions are matched case-insensitively, so `.JPG` and `.jpg` are treated alike.
- Segmented encoding runs in a single pass, and verbose levels were reworked so `-v` means one thing at every depth.

### Fixed

- Crash handling is safe against the loader lock and reports more of the state that led to the crash.
- `--help` colours no longer flash before settling.
- The pack finalizing indicator no longer jumps between archives or lingers after completion.
- Video info parsing tolerates malformed ffprobe output instead of aborting the run.

## [2.0.0-alpha.2] - 2026-08-19

### Added

- `preview` renders an original-versus-encoded comparison video, and accepts a single input to derive the encoded path itself.
- Encoding plans are probed and printed before work starts, and `--dry-run` stops after printing them.
- Probe results are cached, so a repeated run over the same inputs skips re-probing.
- Duplicate media is stored once when packing, instead of once per output archive.
- Structured log records correlate an item's stages — every record carries the same per-run `run_id`, and task records name the task and its input — so a single item's history can be read out of an interleaved log.
- `encro` builds and runs on POSIX systems, not only Windows.

### Changed

- **Breaking:** the default job-state file moved into a hidden `.encro` directory beside the work root, and multiple inputs without a common parent directory now require `--output` instead of falling back to a temporary directory.
- Picture compression resumes: an interrupted JPEG pass continues where it stopped rather than starting over.
- Video encoding efficiency improved, and NVENC quality is tuned per resolution.
- The plan output was rewritten to be readable, and diagnostics from failed piped runs no longer interleave bar frames.
- CLI parsing accepts positional inputs alongside `-i` / `-I`.

### Fixed

- Errors that previously vanished in quiet or non-interactive runs are now visible in every mode.

## [2.0.0-alpha.1] - 2026-08-03

### Added

- Segmented video encoding with frame-level resume: an interrupted encode restarts from the last completed segment rather than from the beginning.
- Progress bars estimate the remaining time with a moving average, and long labels scroll instead of being truncated.
- `-hh` shows every option while `-h` shows the common ones.
- A job-state file that no longer matches the current run is reported before it is discarded.

### Changed

- **Breaking:** `-v` / `--verbose` now echoes progress detail to the terminal and repeats for full debug output; `-e` / `--verbose-echo` and `--flat` were removed.
- NVENC quality defaults to cq 28 and bitrate caps are chosen per resolution.
- The log file is always written, and the terminal becomes an echo of it rather than the only copy.

## [1.7.0] - 2026-05-23

### Added

- Every run writes its own timestamped log file, with rotation and retention, and falls back predictably when the preferred location is unwritable.
- `--log-json` emits one JSON object per line for machine consumption, alongside the human-readable log.
- Error records carry a context chain naming the pipeline stages that led to the failure, and timed scopes report how long each stage took.
- A run snapshots its environment into the log, so a bug report carries the state it happened in.

### Fixed

- The crash handler can still append to the log after the logging backend has shut down.

## [1.6.0] - 2026-05-10

### Added

- `--version` reports the build the binary came from.
- `--help` is coloured and laid out per option group.

### Changed

- Argument parsing moved from Boost.Program_options to CLI11, which changes the wording of usage and validation errors.
- The default answer for interactive confirmation prompts changed from N to Y.
- Packs can be given explicit entry inputs instead of only being derived from the output directory.

## [1.5.0] - 2026-05-05

### Changed

- The answer to an interactive confirmation is remembered for the rest of the run instead of being asked again.

## [1.4.0] - 2026-05-01

No user-visible changes: this milestone reworked the internal packing request into a declarative structure and removed an abstraction layer nothing polymorphically used.

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

[unreleased]: https://github.com/Arteiimis/encro/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/Arteiimis/encro/compare/v2.0.0-rc.1...v2.0.0
[2.0.0-rc.1]: https://github.com/Arteiimis/encro/compare/v2.0.0-alpha.3...v2.0.0-rc.1
[2.0.0-alpha.3]: https://github.com/Arteiimis/encro/compare/v2.0.0-alpha.2...v2.0.0-alpha.3
[2.0.0-alpha.2]: https://github.com/Arteiimis/encro/compare/v2.0.0-alpha.1...v2.0.0-alpha.2
[2.0.0-alpha.1]: https://github.com/Arteiimis/encro/compare/v1.7.0...v2.0.0-alpha.1
[1.7.0]: https://github.com/Arteiimis/encro/compare/v1.6.0...v1.7.0
[1.6.0]: https://github.com/Arteiimis/encro/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/Arteiimis/encro/compare/v1.4.0...v1.5.0
[1.4.0]: https://github.com/Arteiimis/encro/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/Arteiimis/encro/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/Arteiimis/encro/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/Arteiimis/encro/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Arteiimis/encro/compare/v0.4.0...v1.0.0
[0.4.0]: https://github.com/Arteiimis/encro/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Arteiimis/encro/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Arteiimis/encro/compare/v0.1.5...v0.2.0
[0.1.5]: https://github.com/Arteiimis/encro/compare/v0.1.0...v0.1.5
[0.1.0]: https://github.com/Arteiimis/encro/releases/tag/v0.1.0
