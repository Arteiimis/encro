# encro

[![CI](https://github.com/Arteiimis/encro/actions/workflows/ci.yml/badge.svg)](https://github.com/Arteiimis/encro/actions/workflows/ci.yml)

encro is a batch media processing CLI built on top of ffmpeg. Point it at a
directory of videos and it transcodes them to HEVC/H.264 in parallel, with
per-file progress bars and a job store that lets you resume an interrupted
run where it stopped (`--resume`). In picture mode it converts images to
WebP or recompresses JPEGs (quality 2–31, lower is better), and either mode
can pack its results into ZIP archives — or pack any directory directly with
`--pack-only`. Output lands as structured NDJSON logs (`--log-json`) or
human-readable progress bars, whichever fits your pipeline.

## Quick start

Requirements: [xmake](https://xmake.io), ffmpeg + ffprobe on `PATH` (or point
to them with `--ffmpeg-path`).

```sh
xmake build encro

# Transcode all videos in a directory to MP4 (HEVC)
xmake run encro -t video -i ./videos -o ./out

# Convert photos to WebP
xmake run encro -t picture -f webp -i ./photos -o ./out

# Zip everything in a directory
xmake run encro -z -i ./assets -o ./out

# Resume an interrupted job
xmake run encro -t video -i ./videos -o ./out --resume
```

> Note: `xmake run encro` passes extra arguments directly to the program;
> do not use `--` before them.

## Usage

| Flag | Description |
| ---- | ----------- |
| `-t, --type video\|picture` | process type (default `video`) |
| `-i, --input PATH` / `-I, --inputs` | input file or directory / multiple videos |
| `-o, --output PATH` | output directory (`+` = input root, `=` = common root) |
| `-f, --output-format mp4\|webp` | target format (default `mp4`) |
| `-c, --compress` | JPEG compression in picture mode (`-q 2..31`, lower = better) |
| `-p, --pack` / `-z, --pack-only` | pack encoded outputs / pack without encoding |
| `-j, --jobs N` | max parallel jobs (default 10) |
| `--resume` / `--restart` | continue a previous job / discard state and start fresh |
| `--crf N` | video encode quality 0–51 (default 28) |
| `--video-codec` | encoder: `hevc_nvenc` (default), `libx265`, `libx264` |
| `--preset p1..p7` | NVENC preset (auto by resolution) |
| `-s, --folder-summary` | folder summary images in flat packs |
| `-r, --recursive` | recursive input search |
| `-w, --overwrite` / `-y, --yes` | overwrite files / auto-confirm prompts |
| `--keep` | preserve relative subdirectories in output (default: flatten) |

Run `encro -hh` for the full option list, `encro -h` for the brief view.

## Building

C++26, xmake build system. Primary platform is Windows (clang-cl + lld-link);
Linux (clang + lld) is CI-tested.

```sh
xmake build encro          # debug build
xmake f -m release -y      # switch to release (LTO)
xmake build encro

xmake build tests          # unit tests (Catch2)
xmake run tests            # "[tag]" filters a single test

xmake build encro encro_e2e_tool # e2e uses a fake ffmpeg/ffprobe
xmake build e2e_tests
xmake run e2e_tests
```

Build modes: `debug` (all log levels), `release` (LTO, TRACE/DEBUG stripped),
`releasedbg` (optimized + ASan), `coverage` (`xmake coverage` for
instrumented coverage runs). Format with `xmake format` (clang-format).

## Development

- Follow the style rules enforced by clang-format (East const, trailing
  return types, snake_case files / PascalCase types).
- Features are TDD: failing test first, minimal implementation, one commit
  per change together with its tests.

## License

[MIT](LICENSE)
