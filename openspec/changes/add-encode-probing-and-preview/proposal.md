# Add Encode Probing and Preview

## Why

HEVC encoding currently uses a fixed `cq 28` with resolution-based preset/maxrate heuristics, so the final size/quality balance depends almost entirely on content complexity, which resolution cannot predict. Users get no evidence about the outcome before committing to an encode (rework is expensive), and no tool to visually verify quality afterward. The tool already has the pattern to fix this: the WebP path adapts quality in a measure-and-retry loop; the video path has not.

## What Changes

- **Pre-encode quality probing (default on)**: before the existing `(Y/n)` encode-confirmation prompt, the tool probes every pending video (a few short segments at candidate CQ values), measures quality with p5-percentile VMAF, and picks the highest CQ meeting the quality floor. A per-file plan (chosen CQ, measured quality, estimated output size) is printed before the prompt, so the user confirms an informed plan instead of a blind default.
- **Quality floor knob**: new `--min-vmaf` option (default 95, p5 percentile). Explicit `--crf N` bypasses probing entirely (current fixed-CQ behavior).
- **`--dry-run`**: probe and print the plan, then exit without prompting or encoding.
- **Warnings surfaced**: files whose floor is unreachable (e.g. bitrate capped by the resolution maxrate) are warned at plan time and re-listed in the final summary ("needs attention" list).
- **New `preview` subcommand**: `encro preview <original> <encoded>` generates a side-by-side comparison video from 5 uniformly sampled 10-second windows (scored with VMAF, worst window marked in the list), labels each segment with time range and VMAF, and opens the result in the default player. `--start/--duration` switches to manual window mode; `--output` defaults to `<original-dir>/<original-stem>.preview.mp4`; `--no-open` disables auto-open.
- **CLI restructure**: the single-command CLI becomes subcommand-based (`encro preview ...`, with bare `encro ...` falling through to the existing encode behavior, so all current invocations keep working). `preview` as the first argument now resolves to the subcommand instead of being treated as a positional input path.
- **Post-encode hint**: the encode summary prints a one-line hint (`encro preview <original> <encoded>`) pointing to the comparison tool.

## Capabilities

### New Capabilities

- `video-encode-probing`: pre-encode quality probing with a p5-VMAF quality floor, per-file CQ decisions, plan presentation before the confirmation prompt, and dry-run mode.
- `video-preview`: the `preview` subcommand — representative segment selection, VMAF scoring, side-by-side comparison video generation with labels, output defaults, and auto-open behavior.

### Modified Capabilities

- `cli-positional-input`: subcommand names take precedence over positional input interpretation.

## Impact

- `src/video/`: probe orchestration (before the confirmation gate in `video_batch_execution` / `video_process`), per-file CQ decision plumbing into `EncodeConfig`, shared segment-VMAF measurement helper (`video_info` / new helper), warning collection into the summary.
- `src/cmd/`: subcommand structure, new flags (`--min-vmaf`, `--dry-run`, preview options), fallthrough compatibility.
- `src/preview/` (new module): preview planning (windows, scoring), filtergraph building, execution, auto-open.
- `src/infra/`: new `openWithDefaultApp()` helper (ShellExecute).
- Tests: unit tests (probe decision logic, filtergraph builder, CLI), e2e with the fake ffmpeg/ffprobe tool, `[real-ffmpeg]` smoke tests.
- No new dependencies; libvmaf already present in the required ffmpeg build (SSIM fallback when unavailable or for HDR).
