# Design: Encode Probing and Preview

## Context

- Encoding is segmented (10s, `kSegmentDurationUs`), single `EncodeConfig` per segment built from `ctx.config`; `buildEncodeConfig` picks preset/maxrate by resolution and uses a fixed CQ. The confirmation gate lives in `runEncodingTasks` (`readUserIpt` with `yesToAll`).
- The WebP path already runs a measure-and-retry loop (`encodeWebpWithTargetSize`), proving the pattern; the video path has no feedback.
- CLI is a single CLI11 app with a data-driven flag registry; inputs come from one positional option group.
- Required ffmpeg builds ship `libvmaf` (verified locally); `ssim` is built in everywhere. `-progress` parsing, `exec2`, `quoteToolPath`, and `getVidInfo` are reusable.
- e2e tests run against a fake ffmpeg/ffprobe (`ENCRO_FAKE_*` env), which returns generic success and writes dummy output files.

## Goals / Non-Goals

**Goals:**
- Informed confirmation: evidence-based per-file CQ decisions before the existing prompt.
- One shared segment-quality measurement used by both probing and preview.
- A preview subcommand with bounded generation cost (~1 minute) regardless of video length.
- Zero new dependencies; backward-compatible CLI.

**Non-Goals:**
- Post-encode quality reporting as a gate (explicitly rejected).
- Full-length preview generation (explicitly rejected: too slow).
- Persisting probe results across runs; re-probing is deterministic and cheap enough.
- HDR-correct VMAF (tonemapping); HDR falls back to SSIM.
- Previewing WebP/picture outputs.
- Auto-tuning `--maxrate` or preset; those stay resolution-based.

## Decisions

### 1. Probing phase lives inside `runEncodingTasks`, before the prompt

The gate is shared by single-file and batch paths, so placing the probe phase there gives every invocation the same informed prompt. The probe phase SHALL run only for MP4 output (the gate is shared with the WebP path, which keeps its existing adaptive loop). It returns a per-file decision map (`input path -> chosen CQ, p5 score, estimated size, warning`) plus a warning list; after the prompt, `runEncodingTasks` copies each file's decision into the per-task `EncodingState` (new optional `chosenCq` field) when execution contexts are created, and `buildEncodeConfig`/`encodeOneSegment` read that override instead of only `ctx.config.crf`. Warnings ride back to `video_process` alongside `EncodeResultsMap` for `printEncodingSummary`'s "needs attention" list. Stop-signal is checked between probe points, aborting the plan and prompt on cancel (same pattern as the WebP loop).

*Alternative considered*: probing in `video_process` before `runEncodingTasks` (closer to scan, but the single-file path would duplicate it); per-file lazy probing during encoding (spreads cost, but the prompt would come before any evidence — defeats the purpose).

### 2. Probe method: 3 base points + bounded step-4 extension

- Base candidates: CQ {24, 28, 32}. Two 10s segments per candidate, sampled at 25% and 75% of duration (uniform coverage, deterministic). Videos shorter than 40s (two non-overlapping 10s windows) skip probing and use the default CQ.
- Metric: p5 percentile of per-frame VMAF across all probe frames of that CQ.
- Extension: if floor not met at 24, probe 20 (then 16); if floor met at 32, probe 36 (then 40). Bounded to [16, 40]; typical cost 3-6 probe points × 2 segments.
- Decision: highest CQ with p5 >= floor, interpolating linearly between adjacent probed points (rounded down to integer CQ). Bitrate estimate likewise interpolated.
- Unreachable (p5@16 < floor): warn at plan time, encode with CQ 16, add to the "needs attention" list.
- Probe artifacts (segment .ts, progress files, VMAF JSON logs) SHALL go to a per-run temp dir (`fs::temp_directory_path()/encro_probe_<uuid>/`), deleted after the probe phase. They MUST NOT reuse `videoseg::segmentDirForTask`, whose resume scan would mistake leftover probe segments for completed production segments.

*Alternative considered*: binary search over [16,40] — fewer encodes worst-case, but harder to reason about with p5 noise and less predictable printed output; step-4 grid keeps the plan lines intuitive.

### 3. Probe encodes mirror production settings

Probe segments are encoded through the same `EncodeConfig` construction path as the real encode (same codec, preset-by-resolution, maxrate cap, `-b:v 0 -rc vbr`), with only `cq` and temp output differing. This is the correctness invariant: a unit test SHALL assert that a probe config equals the production config modulo CQ/output path. If the invariant breaks later, decisions become invalid silently.

### 4. Shared quality helper: `src/video/video_quality.{h,cpp}`

`measureSegmentQuality(ffmpeg, original, encoded, start, duration)` decodes the aligned segment pair and runs `libvmaf`, returning per-frame scores; callers aggregate p5/mean. Fallback chain: VMAF unavailable or HDR (`getVidInfo` bit-depth/transfer detection) → `ssim` filter; score unparsable or both fail → caller falls back to default CQ 28 (encode side) or omits scores (preview side). SSIM floor mapping (p5): 97→0.985, 95→0.980, 90→0.970.

*Alternative considered*: full-video SSIM pass (single metric, no segmentation) — one extra full decode per file, rejected for cost; segment-based measurement is shared by both features anyway.

### 5. CLI: `preview` is the only subcommand; encode flags stay at top level

CLI11 subcommand `preview` is added to the existing app; after parse, `got_subcommand("preview")` routes to `preview::run`. All current flags and the positional group remain on the parent app, so every existing invocation is byte-for-byte unchanged — no migration of the 40+ flag registry into a nested `encode` subcommand, minimal e2e churn. Two follow-on consequences: (a) `preview -h` must render through a formatter branch that lists the preview flags (the parent's custom formatter is built around the four hardcoded option groups); (b) `appentry::run` must branch to `preview::run` BEFORE `buildAppConfig` (which hard-fails without an input path) and must skip job-state setup for preview.

*Alternative considered*: full `encode`/`preview` subcommand split with fallthrough — cleaner taxonomy, but moves the entire data-driven registry and rewrites every e2e invocation for zero user-visible gain.

### 6. Preview pipeline: score, then one filtergraph pass

Phase A (selection): sample 5 windows of 10s uniformly (or `--start/--duration` single window), run `measureSegmentQuality` per window, print the list marking the worst.
Phase B (generation): a single ffmpeg command builds the comparison video from one filtergraph: per window `trim=start:end,setpts=PTS-STARTPTS` on both inputs, `fps`-normalize to the original's average frame rate, `scale` both to the smaller dimensions (even-rounded), `drawtext` labels (`ORIGINAL`/`ENCODED` via `C\:/Windows/Fonts/arial.ttf`; per-segment label = time range + score), `hstack` each pair, `concat` all windows in time order, then x264 `-crf 14 -preset veryfast` with `-pix_fmt yuv420p` (HDR/10-bit compatibility). Audio follows the same windows: per-window `atrim`/`asetpts` + `concat`, copied when the codec is mp4-compatible, re-encoded to AAC otherwise (same fallback pattern as `buildAudioExtractionCmd`'s aacFallback); no audio stream → silent.

The filtergraph builder is a pure function (`preview_filtergraph.{h,cpp}`) taking probed dims/fps/windows → string, so label escaping and concat structure are unit-testable without ffmpeg. VMAF-scored labels come from Phase A; manual mode omits scores.

*Alternative considered*: two-step temp extraction (extract window pairs to files, then hstack) — simpler graphs but 10+ intermediate files and extra process launches; single-graph trim+concat is standard and avoids intermediates.

### 7. Auto-open via `openWithDefaultApp`

New `infra/open_file.{h,cpp}`: `ShellExecuteW` on the generated path (Windows primary platform). Called unless `--no-open`; e2e tests always pass `--no-open`.

### 8. Defaults and estimates

- `--min-vmaf` default 95 (p5); plan prints chosen CQ, measured p5, estimated size = interpolated probe bitrate × duration + original audio stream size (from ffprobe), and batch totals.
- Preview output default: `<original-dir>/<original-stem>.preview.mp4`, overwritten (`-y`).
- Short videos (< probe budget) skip probing with default CQ 28; preview falls back to full comparison.

## Risks / Trade-offs

- **Probing adds 1-2 min per file before the prompt** → parallelized across the batch, `--crf` bypass, short-video skip; accepted as the price of the guarantee.
- **Probe bitrate slightly overestimates final size** (TS muxing overhead on 10s segments, ~5%) → accepted; the estimate is advisory, and the plan prints it as an estimate.
- **libvmaf's default model is trained on 1080p content** → scores at other resolutions are approximate; acceptable for a relative floor, noted in README.
- **Auto-open is untestable in CI** (ShellExecute side effect) → e2e always passes `--no-open`; the open call itself stays a thin wrapper.
- **Probe determinism across runs** (basis of the resume requirement) is plausible but GPU-dependent → the `[real-ffmpeg]` smoke test re-runs probing on the same input and asserts an identical decision.
- **p5 over 2 segments may misrepresent the whole video** → floor is computed across all probe frames; worst-case underestimates are visible in the plan and adjustable via `--min-vmaf`; accepted trade-off vs. full-pass cost.
- **Probe/production config drift** → single construction path + unit test invariant (Decision 3).
- **drawtext font-path escaping on Windows** (`C\:/` and filtergraph quoting) → pure-function builder with dedicated unit tests.
- **Comparison video double-compresses both sides** → x264 crf 14 is visually transparent for spotting relative differences; documented limitation.
- **`preview` as first argument changes meaning** (previously a positional path) → acceptable per spec; practically a nonexistent path name.
- **Fake-tool e2e cannot produce real VMAF scores** → fallback chain (Decision 4) yields default CQ, keeping fake-tool e2e deterministic; VMAF parsing itself is unit-tested against recorded JSON fixtures.
- **VFR sources** → `fps` normalization to the original's average rate; alignment verified by a `[real-ffmpeg]` smoke test.

## Migration Plan

- No data or state migration; nothing persisted. Existing invocations keep working; the only behavioral change is the inserted probe phase (old behavior exactly: `--crf 28`).
- README documents the new flags, the plan output, and the preview command.
- Rollback: revert the change; no persistent artifacts (preview files are user-visible outputs, disposable).
