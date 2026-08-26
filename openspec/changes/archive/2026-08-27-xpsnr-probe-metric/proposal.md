## Why

Probe scoring runs VMAF (libvmaf) on every probed CQ point, costing ~18s per 10s window (~17x the segment encode itself) and dominating wall time for batch encodes. Calibration measurements across 5 heterogeneous samples show XPSNR tracks VMAF with better slope uniformity than VMAF itself, supports a single global floor mapping with ~±3 VMAF-equivalent error (vs. SSIM whose fixed anchors drift by 10+ points), and scores ~5x faster than VMAF. The SSIM anchor path measured unusable as a default replacement (fixed anchors under-detect quality loss on noisy content by up to 12 VMAF points), so the metric migration goes straight to XPSNR rather than SSIM.

## What Changes

- Replace VMAF as the default probe/window scoring metric with XPSNR (`xpsnr` filter, stats-file parsing; frame score = mean of plane dB values).
- Map the existing `--min-vmaf` floor (unchanged flag name and 0–100 range, semantically "VMAF-equivalent quality floor") onto an XPSNR threshold via a new global anchor table `{90 → 38.5 dB, 95 → 41.0 dB, 97 → 42.5 dB}` with interpolation between anchors (calibrated on 5-sample corpus; documented anime/live-action deviation).
- Keep the fallback chain inside non-HDR scoring: XPSNR fails → VMAF (existing code retained) → SSIM (terminal fallback unchanged). HDR inputs keep going directly to SSIM.
- Probe cache keys include the metric string, so switching metrics naturally invalidates old entries; stored entries gain `"XPSNR"` as a possible value.
- Encoding-plan p5 column and preview window-score output learn the third metric: XPSNR values are dB (can be negative), formatted distinctly from VMAF/SSIM; the plan header keeps describing the floor in its VMAF-scale meaning, and the unreachable-floor warning wording drops its VMAF-specific phrasing (scores may come from any chain metric).
- Not changing: probe windows/segment encoding/CQ sequence/interpolation logic, `--crf` bypass, dry-run behavior, HDR SSIM path, VMAF removal (kept as in-chain fallback).

## Capabilities

### New Capabilities

<!-- none -->

### Modified Capabilities

- `video-encode-probing`: the primary quality metric becomes XPSNR (VMAF demoted to in-chain fallback); the floor is interpreted through a global VMAF→XPSNR anchor table; plan output may render p5 in XPSNR dB units; the cached-decision key now accounts for the XPSNR/VMAF/SSIM metric identity.
- `video-preview`: window scoring (two-input mode and single-input mode descriptions) references the new primary metric chain XPSNR → VMAF → SSIM instead of VMAF → SSIM.

## Impact

- **Code**: `src/video/video_quality.{h,cpp}` (new `runXpsnr`/parser, floor mapping `xpsnrFloorForVmafFloor`, fallback ordering in `measureSegmentQuality`), `src/video/encode_probe.cpp` (floor checks already route via `ssimFloorForVmafFloor`-style mapping, metric labels, `formatProbeP5`, cache-key metric string), `src/video/probe_cache.{h,cpp}` (metric string doc/value), `src/preview/preview_process.cpp` + `src/preview/preview_filtergraph.cpp` (metric label/format branches).
- **Behavior**: first run after upgrade re-probes all previously cached files once (cache keyed by metric); probe phase roughly halves its scoring wall time on the h264/nvenc HEVC workflow; chosen CQ may shift slightly (±3 VMAF-equivalent from global anchors; anime content biased toward smaller CQ/slower encodes at equal floor semantics). One acknowledged determinism edge: upgrading between an interrupted run and its resume invalidates cached decisions mid-resume, so such files re-probe once and may pick a different CQ than the partially-encoded output — same consequence as any decision-affecting settings change, accepted per existing spec.
- **Compatibility**: no CLI/API changes; ffmpeg builds without the `xpsnr` filter degrade to the retained VMAF path with a warning (same pattern as today's VMAF→SSIM fallback).
- **Tests**: unit tests for the XPSNR parser, floor mapping, p5 formatting, and cache round-trip with the new metric string; fake-tool e2e coverage unchanged in shape but scoring-command fixtures grow an xpsnr variant.
