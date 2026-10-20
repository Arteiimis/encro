# encro

[![CI](https://github.com/Arteiimis/encro/actions/workflows/ci.yml/badge.svg)](https://github.com/Arteiimis/encro/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Arteiimis/encro)](https://github.com/Arteiimis/encro/releases/latest)

encro is a batch media processing CLI built on top of ffmpeg. Point it at a directory of videos and it transcodes them to HEVC/H.264 in parallel with per-file progress bars, and a job store lets an interrupted run continue where it stopped (`--resume`). In picture mode it converts images to WebP or recompresses JPEGs (`-q 2..31`, lower is better) and turns videos mixed into the image set into animated WebP (`--video-webp`). Either mode can pack its results into ZIP archives, and `--pack-only` packs a directory as it stands. Output is progress bars or NDJSON records (`--log-json`).

## Installing

Every downloadable file is on the [Releases](https://github.com/Arteiimis/encro/releases) page; take the newest one. `2.0.0` and everything below it were tagged retrospectively, so those entries record history rather than offer a download.

**Windows**, through Scoop:

```sh
scoop bucket add encro https://github.com/Arteiimis/scoop-encro
scoop install encro
```

Or unpack `encro-<version>-x64-windows.zip` anywhere and run `bin\encro.exe`.

**Linux**: download `encro-<version>-x86_64-linux.tar.gz`, unpack it anywhere, and add its `bin/` to `PATH`. It is built on Ubuntu 24.04, so it needs glibc 2.38 or newer (Ubuntu 24.04+, Debian 13+, Fedora 39+) and does not start on Ubuntu 22.04.

To upgrade, `scoop update` refreshes the bucket first, then `scoop update encro` installs the new version; on Linux, unpack the newer tarball over the old directory. `encro --version` prints the build you have — `-v` alone is verbose output, not the version.

Both platforms need **ffmpeg and ffprobe** installed and reachable — on `PATH`, or named with `--ffmpeg-path` (`-x`). encro drives them and does not bundle them.

### What each build can do

| | Windows | Linux |
| --- | --- | --- |
| video, picture, packing, `batch`, `preview`, `config`, `completion` | yes | yes |
| `organize` | yes | **no inference runtime** |

On Linux, `organize` is not offered. It needs a local inference backend and only the Windows build links one, so `encro -h` and the generated completion scripts leave it out there, and `encro organize <dir>` exits non-zero with an error naming the missing backend before it scans, copies or downloads anything. Use a Windows build to sort images. If you already installed a completion script, re-run `encro completion <shell> --install` to drop the stale `organize` candidates.

The Windows archive carries everything the binary needs to start, including the Visual C++ runtime libraries it imports (redistributed under the Microsoft licence) — ffmpeg is the only prerequisite left. Each release also publishes a `-symbols.zip` beside it: unpack that next to `bin\encro.exe` and a crash report resolves to function names.

### Archive layout

The archive root holds `LICENSE`, `README.md` and `bin/` (`bin/encro`, or `bin\encro.exe` on Windows). Keep the directory intact — the runtime libraries sit beside the executable.

## Quick start

The examples assume `encro` is on your `PATH`. From a source checkout, build first (see [Building](#building)) and use `xmake run encro` in place of `encro`.

```sh
# Transcode all videos in a directory to MP4 (HEVC)
encro -t video -i ./videos -o ./out

# Convert photos to WebP
encro -t picture -f webp -i ./photos -o ./out

# Pack a mixed set: recompress the images, convert the clips to animated WebP,
# and put both in the same archives
encro -t picture --video-webp -c -i ./photos

# Zip everything in a directory
encro -z -i ./assets -o ./out

# Resume an interrupted job
encro -t video -i ./videos -o ./out --resume

# Process every first-level subfolder of a directory as its own task
encro batch ./releases -t picture -c
```

## Shell completion

Tab completion for options, enum values, and config keys is generated from the CLI itself, so it always matches your build.

```sh
# PowerShell — install (one guarded block per profile:
# Windows PowerShell 5.1 and PowerShell 7)
encro completion powershell --install

# Git Bash / Linux bash — install (bash-completion lazy-load dir, or .bashrc)
encro completion bash --install
```

`--uninstall` removes everything `--install` created; re-run `--install` after upgrading encro to refresh the embedded candidates. `encro completion bash` and `encro completion powershell` print the script to stdout without installing anything.

> Note: on Git Bash the bash installer targets `%USERPROFILE%` (the Git-for-Windows default `HOME`); with a custom MSYS `HOME`, wire the printed script manually.

## Where encro keeps its working files

During a run, encro writes intermediates to three places:

- `<work-root>/.encro/` — the hidden work directory at the output root (or the inputs' common ancestor, or `--output` when given). It holds the per-task segments and caches of a run plus the `job-state.json` a `--resume` reads; on success the caches are removed and the directory is kept so an interrupted run can continue.
- `<temp>/encro/scratch/` — per-run transient files, safe to delete; anything untouched for over 24 hours is swept at startup. `<temp>` is `%TEMP%` on Windows and the platform temp dir (`$TMPDIR` or `/tmp`) elsewhere.
- `%LOCALAPPDATA%\encro\` on Windows, `~/.local/state/encro/` elsewhere (`$XDG_STATE_HOME` when set) — rotating logs under `logs/` and the probe cache (`probe-cache.json`, override with `ENCRO_PROBE_CACHE`).

The model files `organize` needs live apart, under `~/.encro/models/` (`%USERPROFILE%\.encro\models` on Windows; ~530 MB, fetched by `--download-models`, moved with `--model-dir`). The user config file has its own location — see [`encro config`](#persisting-defaults-encro-config).

## Usage

| Flag | Description |
| ---- | ----------- |
| `-t, --type video\|picture` | process type (default `video`) |
| `-i, --input PATH` / `-I, --inputs` | input file or directory / multiple videos |
| `-o, --output PATH` | output directory (`+` = input root, `=` = common root) |
| `-f, --output-format mp4\|webp` | target format (default `mp4`) |
| `-c, --compress` | JPEG compression in picture mode (`-q 2..31`, lower = better) |
| `--video-webp` | convert videos in a picture input to animated WebP |
| `-p, --pack` / `-z, --pack-only` | pack encoded outputs / pack without encoding |
| `-j, --jobs N` | max parallel jobs (default 10) |
| `-x, --ffmpeg-path DIR` | ffmpeg/ffprobe install directory (default: `PATH`) |
| `--resume` / `--restart` | continue a previous job / discard state and start fresh |
| `--state-file PATH` | job-state location (default `<work-root>/.encro/job-state.json`) |
| `--crf N` | video encode quality 0–51 (default 28; bypasses probing) |
| `--min-vmaf N` | p5-VMAF quality floor for probing, 0–100 (default 95) |
| `--dry-run` | probe and print the encoding plan, then exit without encoding |
| `--video-codec` | encoder: `hevc_nvenc` (default), `libx265`, `libx264` |
| `--preset p1..p7` | NVENC preset (auto by resolution) |
| `-s, --folder-summary` | folder summary images in flat packs |
| `-r, --recursive` | recursive input search |
| `-y, --yes` | auto-confirm prompts |
| `--keep` | preserve relative subdirectories in output (default: flatten) |
| `--force-conflict-handling y\|n` | flat-output name collisions: `y` renames, `n` allows duplicates |

Run `encro -hh` for the full option list, `encro -h` for the brief view.

### Persisting defaults (`encro config`)

Preference options can be stored in a user-level config so they do not have to be repeated on every command line. Precedence is always command line > template > config file > built-in default.

```sh
encro config set crf 23      # store (validated like the CLI option)
encro config set jobs 4
encro config list            # all keys, values, and sources
encro config get crf         # effective value of one key
encro config unset crf       # back to the built-in default
encro config path            # where the file lives
```

The file is pretty-printed JSON at `%LOCALAPPDATA%\encro\config.json` (Windows) or `~/.config/encro/config.json` (POSIX, `$XDG_CONFIG_HOME` when set); point `ENCRO_CONFIG` at another path to override. Persisted boolean flags (`pack`, `yes`, `keep`, `compress`, `recursive`, `folder-summary`) can be turned off for a single run with their negation form, e.g. `--no-pack`.

### Per-task shapes (`encro template`)

Where `config` holds this machine's preferences, a template holds the argument cluster that shapes one kind of task — and it is per task, so "anime" and "phone clips" no longer overwrite each other. The command line still speaks last.

```sh
encro template save anime -t video --video-codec hevc_nvenc --preset p5 --crf 24 --keep -p
encro -T anime D:\shows          # the whole cluster above, plus the path
encro -T anime --crf 20 D:\shows # one option overridden for this run
encro -T anime --no-pack D:\shows # one stored boolean cleared for this run
encro template list              # names and what each one stores
encro template show anime        # the file itself
encro template unset anime crf   # drop one key, fall back to config/default
encro template delete anime
encro template path              # where the files live
```

Every value and boolean flag of the main command is templateable except input paths, `--state-file`, `-v` and `-T` itself: paths stay on the command line because they are what changes between runs. Template values behave as defaults, so a stored `--dry-run` never collides with an explicit `--crf`, and any stored boolean can be cleared with its `--no-…` form. A second `save` merges instead of replacing, so `encro template save anime --crf 22` only touches `crf`.

Templates live one file per name in `templates/` beside the config file, in the same JSON shape as `config.json`. Each file references `templates.schema.json`, which `save` writes beside it from the binary's own option table (editors pick it up for validation and completion; `encro template schema` prints it).

### Quality probing (MP4 encodes)

Before the confirmation prompt, encro probes each MP4 encode: it encodes two 10-second windows per candidate CQ, measures p5-percentile VMAF against the source, and picks the highest CQ that meets the `--min-vmaf` floor (95 by default; SSIM is used when VMAF is unavailable, e.g. HDR). The resulting per-file plan — CQ, p5 score, estimated size and compression ratio — is printed before you confirm. Pass `--crf` to skip probing and use a fixed quality, or `--dry-run` to print the plan and exit without encoding. Short videos (< 40s) are not probed and use the default CQ.

### Comparing original vs encoded (`preview`)

```sh
encro preview <original> [<encoded>] [--start S --duration D] [--output PATH] [--no-open]
```

`preview` samples five 10-second windows (or one `--start`/`--duration` window), scores each with VMAF/SSIM, marks the worst, and renders a comparison video with ORIGINAL/ENCODED panes and per-window labels. Output defaults to `<original-stem>.preview.mp4` next to the original and opens in your default player unless `--no-open` is passed. Videos shorter than 50 seconds are compared in full. WebP inputs are rejected: preview compares videos only.

With one input, `preview` runs the probe phase on the source, encodes the windows with the production settings at the chosen CQ, and compares against those — so you can see what the probe-selected configuration will look like before committing to a full encode. With two inputs, it compares an existing original with its encoded output.

### Grouping images by character (`organize`)

```sh
encro organize <dir> [-r] [--min-confidence F] [--model-dir <dir>] [--identity-tau F] [--ingest <folder>] [--ignore-folder <folder>] [--download-models] [--dry-run]
```

`organize` sorts a folder of images into per-character folders under `<dir>/organized/` using two local AI models (a tagger and an identity model). Analysis is fully local: image content never leaves this machine. Known characters file by their tag (`organized/hatsune_miku/`); everything else clusters by appearance into `unknown_<tags>/` folders; multi-subject images land in `mixed/`. Originals are always copied, never moved.

When `<dir>` already holds first-level folders (and `-r` is not given), the run is **incremental**: each first-level folder becomes a reference to learn from — up to 20 of its images are sampled (by content hash, and cached) — and matching images file into `<dir>/organized/<that folder's name>/`, mirroring your structure without touching it. Folders named like a miscellaneous bin (`mix`, `杂项`, `その他`, …) are treated as input instead: their images are sorted together with the loose ones. Dot-prefixed folders are skipped. `--ingest <folder>` forces any first-level folder to be input, `--ignore-folder <folder>` skips one entirely; both are repeatable. A reference whose sample splits into two distinct characters is demoted with a hint in the report. With `-r`, the run keeps its whole-tree behavior: every first-level folder is scanned as input and nothing is mirrored.

The first run needs the model files (~530 MB, one time):

```sh
encro organize <dir> --download-models
```

`--download-models` without `<dir>` only fetches the models, so name a folder when you also want the run.

`--identity-tau F` sets how similar two images must be to share a folder, by identity feature and identity-bearing tags together (default `0.70`, or `0.74` when neither side has tag evidence). It is a calibrated operating point: raise it to keep look-alikes apart, lower it to hold one character's varied artwork together.

A first run already groups what it can: the character folder this run's own tag routing creates becomes a reference, so a matching cluster is filed there in the same run instead of waiting for a second one.

Rename any output folder to rename a character — e.g. `hatsune_miku` → `初音ミク`; later runs file new images of that character under your name. `--dry-run` prints the plan without copying, and results are cached by content hash, so re-runs re-analyze nothing (`--recluster` discards the cache). The release build carries no CUDA provider, so `organize` uses the providers built into the inference runtime; for an NVIDIA GPU, put `onnxruntime_providers_cuda.dll` and `onnxruntime_providers_shared.dll` next to the executable and install the CUDA runtime (`scoop install versions/cuda12.9`; `--download-models` fetches cuDNN), since provider libraries load only from the executable's own directory. Otherwise everything runs on the CPU with a notice.

### Batch processing (`batch`)

```sh
encro batch <root> [processing options]
```

`batch` treats every first-level subdirectory of `<root>` as its own task and runs the single-directory pipeline over each one, in name order, with the same processing options — one task type per run, so `-t video` encodes every subfolder and `-t picture -c` recompresses each one. Hidden directories, the packed root and loose files at the top level are not tasks, and a subfolder with nothing for the current options is skipped with a notice instead of failing the run.

Packed archives land under `<root>/packed/<subfolder>/`, and `-o/--output` overrides that packed root as a plain directory path: the single-run alias forms (`+`, `=`, `input://`, `common://`) are rejected, because a batch has no single input root to resolve them against. So are `-I/--inputs` and `--state-file` (each subfolder keeps its own state under its own work root); every other option follows the single-directory rules.

Each subfolder keeps its own state, so `--resume` continues a batch subfolder by subfolder and a state mismatch is discarded for that subfolder alone. A subfolder that fails does not stop the batch, a stop request ends it at the current subfolder's safe point, and the run closes with a per-subfolder summary. The exit code is non-zero exactly when a subfolder failed; a canceled batch keeps the cancellation exit code.

ffmpeg and ffprobe are resolved once before the first subfolder, so a missing tool fails the batch before any work starts (`--pack-only` needs no tool at all).

The confirmation prompt is asked once, by the first subfolder that reaches it, and that answer covers the rest; declining ends the batch and exits 0. `--yes` skips it — the mode to use in scripts.

## Building

C++26, xmake build system. Primary platform is Windows (clang-cl + lld-link); Linux (clang + lld) is CI-tested. `xmake run encro <args>` runs the build; it forwards extra arguments straight to the program, so never insert `--` before them.

```sh
xmake build encro          # debug build
xmake f -m release -y      # switch to release (LTO)
xmake build encro

xmake test-report          # the test entry point: builds and runs both suites, writes a report

xmake build tests          # or drive them by hand (Catch2)
xmake run tests            # "[tag]" filters a single test

xmake build encro encro_e2e_tool # e2e uses a fake ffmpeg/ffprobe
xmake build e2e_tests
xmake run e2e_tests
```

Build modes: `debug` (all log levels), `release` (LTO, TRACE/DEBUG stripped), `releasedbg` (optimized + ASan), `coverage` (instrumented, via `xmake coverage`). Format with `xmake fmt` (clang-format); `xmake tidy` reports static analysis.

## Development

Contributor conventions — code style, test rules, the OpenSpec workflow, commit format — are in [AGENTS.md](AGENTS.md). A red CI run is diagnosed from the failed step's log first, then from the `test-reports-<mode>` artifact (`gh run download <run-id> -n test-reports-<mode>`, which carries the JUnit file and the per-task encro logs); [docs/ci-debugging.md](docs/ci-debugging.md) has the traps worth knowing before re-deriving them.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — what each version changed.
- [docs/public-api.md](docs/public-api.md) — the CLI contract: exit codes, `config.json`, `job-state.json` and `--log-json` records.

## License

[MIT](LICENSE)
