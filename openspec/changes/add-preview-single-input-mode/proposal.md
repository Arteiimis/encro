# Add Single-Input Preview Mode

## Why

`encro preview` currently requires two positional arguments (`<original> <encoded>`) and compares two existing files — useful for verifying a finished encode, but too late to influence the encode decision. The user's actual workflow is: before committing to a full encode, confirm what the probe-selected configuration (chosen CQ) will look like. With probing already default-on, the tool knows the chosen CQ before the confirmation prompt — it should be able to show a preview of that configuration on the real source, without requiring a completed encode.

## What Changes

- **Single-input preview**: `encro preview <original>` (one positional) runs the probe phase on the original, encodes the preview windows with the production encode settings at the chosen CQ (mirroring probe segments: same codec, preset-by-resolution, maxrate; only CQ and output differ), scores each window with VMAF/SSIM, and renders the same side-by-side comparison video as the two-input mode (5 uniform 10s windows, worst-window marking, ORIGINAL/ENCODED labels, auto-open, `--output`/`--no-open`/`--start`/`--duration`).
- **Two-input mode unchanged**: `encro preview <original> <encoded>` keeps comparing two existing files; the post-parse validation relaxes from "exactly two" to "one or two".
- **Zero positionals still errors**: `encro preview` alone reports the missing-argument error.
- **Short videos / probe failure**: when probing cannot determine a CQ (video < 40s, scoring failure), the preview renders with the default CQ 28 — same degradation as the encode path, printed as an informational note.
- **Post-encode summary hint** and README usage text updated to mention the single-input form.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `video-preview`: preview accepts one or two positionals; with one input it probes the source and renders the comparison against windows encoded with the chosen CQ; window scoring reuses the shared quality measurement (VMAF with SSIM fallback).

## Impact

- `src/preview/preview_process.{h,cpp}`: optional encoded input; single-input branch (probe → encode windows at chosen CQ → score → render).
- `src/preview/preview_filtergraph.{h,cpp}`: the encoded side becomes a list of segment inputs with segment-local timestamps (trim `[0, duration]` per window) instead of one full-file input (trim `[start, end]`).
- `src/cmd/cmd.cpp`: positional validation accepts one or two; preview help text.
- `src/video/encode_probe.{h,cpp}`: reuse `probeSingleFile` (already public) and the probe segment encode path; probe artifacts go to a per-run temp dir under the preview flow, cleaned up after use.
- Tests: cmd parsing (one/two/zero positionals), filtergraph segment-mode strings, preview_process fake-toolchain single-input run, e2e fake-toolchain single-input preview, `[real-ffmpeg]` smoke.
- No new dependencies.
