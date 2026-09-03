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

## Shell completion

Tab completion for options, enum values, and config keys is generated from the
CLI itself, so it always matches your build.

```sh
# PowerShell — install (adds one guarded block to your profile)
xmake run encro completion powershell --install

# Git Bash / Linux bash — install (bash-completion lazy-load dir, or .bashrc)
xmake run encro completion bash --install
```

`--uninstall` removes everything `--install` created; re-run `--install` after
upgrading encro to refresh the embedded candidates. `encro completion bash`
and `encro completion powershell` print the script to stdout without
installing anything.

> Note: install targets `%USERPROFILE%` (the Git-for-Windows default `HOME`).
> If you run a custom MSYS `HOME`, wire the printed script manually.

## Where encro keeps its working files

During a run, encro writes intermediates to three places:

- `<work-root>\.encro\` — the hidden work directory at the output root (or
  the inputs' common ancestor, or `--output` when given): per-task video
  segments (`segments\`), the picture compression cache (`compress_q<N>\`)
  and the job-state file (`job-state.json`). On success the per-task segment
  dirs and the compression cache are removed; the directory itself is kept so
  an interrupted run can be continued with `--resume`. Dot-prefixed names
  keep it invisible on POSIX; on Windows it carries the Hidden attribute.
- `%TEMP%\encro\scratch\` — per-run transient files (probe segments,
  VMAF/SSIM logs, progress files). Entries untouched for over 24 hours are
  swept at startup.
- `%LOCALAPPDATA%\encro\` — rotating logs, the probe cache, and the user
  config file (`config.json`, written by `encro config set`).

> **Breaking change:** the default job-state path moved to
> `<work-root>\.encro\job-state.json`. Pass `--state-file` for a custom
> location. Multiple inputs without a common directory (e.g. cross-drive)
> now require `--output`.

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

### Persisting defaults (`encro config`)

Preference options can be stored in a user-level config so they do not have
to be repeated on every command line. Precedence is always
command line > config file > built-in default.

```sh
encro config set crf 23      # store (validated like the CLI option)
encro config set jobs 4
encro config list            # all keys, values, and sources
encro config get crf         # effective value of one key
encro config unset crf       # back to the built-in default
encro config path            # where the file lives
```

The file is pretty-printed JSON at
`%LOCALAPPDATA%\encro\config.json` (Windows) or
`~/.config/encro/config.json` (POSIX); point `ENCRO_CONFIG` at another path
to override. Persisted boolean flags (`pack`, `yes`, `keep`, `compress`,
`recursive`, `folder-summary`) can be turned off for a single run with their
negation form, e.g. `--no-pack`.

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
instrumented coverage runs). Format with `xmake fmt` (clang-format).

## Development

- Follow the style rules enforced by clang-format (East const, trailing
  return types, snake_case files / PascalCase types).
- Features are TDD: failing test first, minimal implementation, one commit
  per change together with its tests.

## Debugging CI failures

A failed CI job leaves three places to look, in order:

1. **The failed step's own log** (GitHub UI, or `gh run view <run-id> --log`
   once the run has finished). E2E failures dump the child's stdout/stderr
   **plus the tail of the encro log file named by `Log file:` in stdout** —
   that tail contains the ffmpeg stderr for the failing command. The final
   `Print debug info locations` step prints a self-contained pointer to the
   artifact with its download command.
2. **The `test-reports-<mode>` artifact** (download from the run page, or
   `gh run download <run-id> -n test-reports-<mode>`). It contains
   `/tmp/ut.xml` (JUnit: failing test names with `file:line`),
   `/tmp/ut.log` (unit-test console), and
   `~/.local/state/encro/logs/` (per-task encro logs, including the ffmpeg
   command lines and their stderr).
3. **Failed unit tests**: the `Run unit tests` step prints the JUnit failure
   entries directly in its log; grep the artifact's `ut.xml` for `<failure>`
   for the same list.

Caveats:

- Job logs are only fetchable after the whole run completes (jobs of an
  in-progress run return empty via the CLI).
- The coverage job uploads no artifact; only its step logs exist.
- spdlog keeps 10 rotating encro logs; if the failing test ran first, its
  log file may already be rotated away by the time the artifact is uploaded.
  The `Log file:`-tail dump in the step log covers this case.

## License

[MIT](LICENSE)
