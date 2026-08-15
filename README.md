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
| `--crf N` | video encode quality 0–51 (default 28; bypasses probing) |
| `--min-vmaf N` | p5-VMAF quality floor for probing, 0–100 (default 95) |
| `--dry-run` | probe and print the encoding plan, then exit without encoding |
| `--video-codec` | encoder: `hevc_nvenc` (default), `libx265`, `libx264` |
| `--preset p1..p7` | NVENC preset (auto by resolution) |
| `-s, --folder-summary` | folder summary images in flat packs |
| `-r, --recursive` | recursive input search |
| `-w, --overwrite` / `-y, --yes` | overwrite files / auto-confirm prompts |
| `--keep` | preserve relative subdirectories in output (default: flatten) |

Run `encro -hh` for the full option list, `encro -h` for the brief view.

### Quality probing (MP4 encodes)

Before the confirmation prompt, encro probes each MP4 encode: it encodes two
10-second windows per candidate CQ, measures p5-percentile VMAF against the
source, and picks the highest CQ that meets the floor (default 95, adjust
with `--min-vmaf`; SSIM is used when VMAF is unavailable, e.g. HDR). The
resulting per-file plan — CQ, p5 score, estimated size and compression
ratio — is printed before you confirm. Pass `--crf` to skip probing and use
a fixed quality, or `--dry-run` to print the plan and exit without encoding.
Short videos (< 40s) are not probed and use the default CQ.

### Comparing original vs encoded (`preview`)

```sh
xmake run encro preview <original> [<encoded>] [--start S --duration D] [--output PATH] [--no-open]
```

`preview` samples five 10-second windows (or one `--start`/`--duration`
window), scores each with VMAF/SSIM, marks the worst, and renders a
comparison video with ORIGINAL/ENCODED panes and per-window labels. Output
defaults to `<original-stem>.preview.mp4` next to the original and opens in
your default player unless `--no-open` is passed. Videos shorter than 50
seconds are compared in full. WebP inputs are rejected: preview compares
videos only.

With one input, `preview` runs the probe phase on the source, encodes the
windows with the production settings at the chosen CQ, and compares against
those — so you can see what the probe-selected configuration will look like
before committing to a full encode. With two inputs, it compares an existing
original with its encoded output.

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
