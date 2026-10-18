# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[unreleased]: https://github.com/Arteiimis/encro/compare/v0.1.5...HEAD
[0.1.5]: https://github.com/Arteiimis/encro/compare/v0.1.0...v0.1.5
[0.1.0]: https://github.com/Arteiimis/encro/releases/tag/v0.1.0
