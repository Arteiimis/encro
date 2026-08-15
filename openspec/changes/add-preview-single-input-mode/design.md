# Design: Single-Input Preview Mode

## 1. CLI: one or two positionals

`cmd.cpp` post-parse validation relaxes from exactly-two to one-or-two: zero positionals still error (`preview requires at least one positional argument: <original> [<encoded>]`). `CmdParseResult` keeps `previewOriginal`/`previewEncoded` with `previewEncoded` staying `std::optional` (already optional in the struct; only the validation changes). `PreviewOptions.encoded` becomes `std::optional<fs::path>`; `preview -h` usage line shows `<original> [<encoded>]`.

## 2. Single-input pipeline (preview_process::run)

Two-input mode is untouched (probe both, compare full files). Single-input mode runs in the same `run()`:

1. `probeVideo(original)` as today → `VideoProbe`.
2. `pickPreviewWindows(shorterDurationUs, manualRange)` as today → N windows (5 uniform, full-comparison, or manual).
3. **Probe** the source with the shared `encodeprobe::probeSingleFile(ctx, original, probeDir, onPoint, onStep)` (already public; the probe phase reuses it per file). `probeDir = fs::temp_directory_path()/encro_preview_<uuid>/` with the same RAII `remove_all` guard as `runProbePhase` — probe segments must not leak into `videoseg` dirs.
4. **Chosen CQ**: `plan.chosenCq` (default 28 when `plan.probed == false` — short video or scoring failure). When not probed, print an informational note (e.g. "probing skipped (short video); previewing at default CQ 28").
5. **Encode N window segments** with the production settings at the chosen CQ: `encodeprobe::buildProbeSegmentConfig` is the shared production-config mirror (same codec/preset/maxrate, differing only in CQ and output path — the config-mirroring invariant already covers it), invoked with `-ss S -i original -t duration` and written to `seg_<i>.ts` in the preview temp dir. Segments carry segment-local PTS (GOP offset) exactly like probe segments.
6. **Score** each window with `videoquality::measureSegmentQuality(ffmpeg, original, seg_i, startUs, durationUs, info, /*encodedHasLocalPts=*/true)` — same call the probe phase makes. Phase A printing (list, worst marked) is unchanged.
7. **Render**: the segment-mode filtergraph (below) + `buildPreviewCommand` with inputs `[original, seg_0..seg_{N-1}]`, same output default/`--output`/`--no-open`/auto-open behavior as two-input mode.

Concurrency: single-input preview is inherently one file; no parallel execution needed. Stop-signal: checked between steps like the probe phase (`stopsignal::isStopRequested()` after probing and after each encode/score step returns the run as canceled).

## 3. Filtergraph segment mode

`buildPreviewFiltergraph` and `buildPreviewCommand` gain an `encodedWindowsAreSegments` flag (default false, two-input mode unchanged):

- **Two-input mode (unchanged)**: `-i original -i encoded`; encoded side is `[1:v]` with per-window `trim=start=S_i:end=E_i` (source timestamps).
- **Segment mode**: inputs are `-i original -i seg_0 ... -i seg_{N-1}`; the encoded chain for window `i` references `[1+i:v]` with `trim=start=0:end=duration` (segment-local PTS — no input seek on segments). The original side is unchanged (full-file trim at source timestamps; no `-ss` — one input serves five windows with different starts).
- Audio, fps normalization, scale/pad, drawtext labels, hstack/concat: unchanged — audio always comes from the original input.

The graph string builder stays a pure function; the flag only changes the encoded-chain input index and trim start, so exact-string unit tests cover both modes.

## 4. Costs

Single-input preview adds the probe phase (already measured ~35s on a 2h video) plus N window encodes (~1s each, NVENC) and N window scores (~3.5s each with the -ss input seek) — a ~1 minute end-to-end confirmation before a potentially multi-hour encode. Probe artifacts and window segments live in the per-run temp dir and are deleted after rendering; nothing touches `videoseg` or the output directory.

*Alternative considered*: render the single-input preview with x264 instead of the production NVENC settings — faster decode everywhere but misleading: the point is to confirm the actual encode configuration. Production settings at the chosen CQ is the whole value of the feature.
