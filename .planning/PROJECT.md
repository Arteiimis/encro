# encro — Project Definition

**Project:** encro — CLI tool for video/picture encoding and zip packing via FFmpeg
**Language:** C++26 (clang-cl)
**Platform:** Windows primary, Linux/macOS supported

## Vision
A fast, resumable CLI tool for batch video encoding and image compression with intelligent packing into zip archives. Users run a single command to encode and pack entire directories.

## Principles
- CLI-first: everything driven by command-line flags
- Progress visibility: users see what's happening
- Resumability: interrupted jobs can continue via persistent state
- No data loss: errors handled explicitly, nothing deleted silently
