## Context

Probe scoring today (`video_quality.cpp::measureSegmentQuality`) tries VMAF first for non-HDR inputs and falls back to SSIM with warning logs; HDR inputs skip directly to SSIM. `encode_probe.cpp` compares scores against the floor through a per-metric mapping (`meetsFloor`/`floorForMetric`, currently VMAF-direct or `ssimFloorForVmafFloor`). The probe cache key already includes the metric string, and `ProbePlan`/preview output carry metric labels. Calibration data (2026-08 session, 5 heterogeneous 1080p-class samples, production-like chain: nvenc hevc p5, two pooled 10s windows, encro p5 rule):

- XPSNR: ~3.7s vs ~18s (VMAF) per window; score span across CQ 16→36 ≈ 5.5–6.9 dB; near-linear dB-vs-CQ slope (step stdev ≈ 0.13–0.15, better than VMAF's).
- Global anchor table measured: `{90 → 38.5 dB, 95 → 41.0 dB}` (+97 → ~42.5 dB, 2 in-range samples); cross-content σ ≈ 1.4 dB ≈ ±3 VMAF points.
- Anime deviates +2.7 dB at the same floor; live-action samples cluster tightly.
- SSIM fixed anchors measured unusable (eff-VMAF drift up to −12 points), which is why this change targets XPSNR directly.
- Known defect found during calibration (out of scope here, tracked separately): frame-misaligned sources (e.g. dup-framed DVD rips) collapse all metrics CQ-independently.

## Goals / Non-Goals

**Goals:**
- XPSNR becomes the primary probe/window scoring metric; VMAF demoted to in-chain fallback; SSIM stays terminal fallback + HDR path.
- Floor semantics unchanged for users: `--min-vmaf` keeps its name/range/VMAF meaning; comparisons happen via a fixed global anchor mapping with interpolation.
- Plan/preview output renders p5 in the active metric's units.
- Cache correctness across the migration without manual steps.

**Non-Goals:**
- No content-adaptive anchors (no per-content/per-genre bucketing in this change).
- No CLI surface changes (`--min-vmaf` name, validation, defaults untouched).
- No change to windows, CQ sequence, interpolation, or segment encoding.
- Not fixing the frame-misalignment defect; not changing HDR handling.
- Not removing the VMAF/SSIM code paths (both remain as fallbacks).

## Decisions

- **D1: Frame score = mean of plane dB values.** The xpsnr filter prints y/u/v (or r/g/b) per frame; using their mean mirrors how ffmpeg summarizes (log-level "XPSNR average" per plane) while keeping one scalar per frame for pooling. Alternative: use luma-only — rejected: chroma carries blockiness visible to VMAF's feature set; mean matches better in calibration.
- **D2: Anchor table `{90→38.5, 95→41.0, 97→42.5}`, linear interpolation between, clamp outside.** Same shape as the existing `kSsimFloorAnchors` machinery, so implementation reuses that pattern (rename/generalize `ssimFloorForVmafFloor` → shared threshold helper). Exact constants live next to the table with a comment citing the calibration corpus and its anime caveat. Alternative: power-law curve fit over all samples — rejected: three anchors with clamping are simpler, auditable, and match measurement resolution.
- **D3: Fallback order XPSNR → VMAF → SSIM inside non-HDR scoring.** Each failed step logs a warning naming the unavailable metric (existing pattern). `QualityMetric` gains an `Xpsnr` value; HDR still routes straight to SSIM before any attempt. Alternative: build-time capability detection to hide xpsnr when absent — rejected: runtime fallback needs no build plumbing and self-heals if the user switches ffmpeg.
- **D4: Metric string `"XPSNR"` joins the cache key payload.** Old entries keyed `"VMAF"` miss naturally on first run after upgrade (one-time re-probe; expected, documented in proposal Impact). `probe_cache.h` comment updated from `"VMAF" | "SSIM"` to include `"XPSNR"`. No schema version bump needed — the key field itself changes so stale entries are simply never matched (they age out via normal eviction).
- **D5: Display formatting dispatches on metric; the plan header stays VMAF-scale.** `formatProbeP5` (and preview equivalents) gain an XPSNR branch: dB values render like `-4.21 dB` / `41.05 dB` — always signed-width-stable decimal with explicit unit so negative or sub-zero values cannot be confused with VMAF numbers. The table header (`printProbePlan`'s `min p5-<metric> <floor>`) stops deriving its metric name from `plans.front().metric`: the floor is VMAF-semantics by definition, so the header wording reflects the floor scale regardless of which metric leads the batch.
- **D7: Probe-point metric consistency guard.** The two scoring windows of one probe point are pooled into one percentile, so mixing metrics would silently mix scales. After both windows score, their metrics must agree; on disagreement the point is discarded with a warning naming both metrics and the file takes the existing probe-failure path (default CQ). Deliberately simple: rescore-to-match adds request-a-metric plumbing for a once-per-upgrade corner case; ponytail: revisit if logs ever show it firing in practice.
- **D6: Fake-tool e2e fixture grows an xpsnr mode** alongside the existing vmaf/ssim impersonations (env-switchable), so e2e can exercise the primary path plus each fallback transition deterministically.

## Risks / Trade-offs

- [Anchor drift on unseen content (anime flagged; corpus is user-collection-shaped)] → Mitigation: anchors centralized behind one function; recalibration = editing three constants after rerunning the external script; fallback chain still yields valid CQs even if thresholds are off.
- [σ≈1.4 dB means chosen CQ may differ by ±3 VMAF-equivalent from VMAF-era decisions] → accepted trade-off, documented in proposal; cache invalidation makes old decisions invisible rather than mixed.
- [`xpsnr` filter missing in some distributions] → runtime fallback to VMAF with a warning; behavior equals today's.
- [Window-level fallback asymmetry pools mixed scales] → D7 discards the point instead of pooling; probe failure degrades to default CQ, already specified behavior.
- [New failure mode in log parsing (per-plane lines, possible missing planes)] → parser keys on regex over whole line like the SSIM parser; unit tests cover malformed/truncated files; unparseable file → fall back down the chain rather than hard-fail the probe.
- [Preview single-input mode inherits the chain automatically via shared helper] → covered by delta spec scenario; low implementation risk since both call `measureSegmentQuality`.

## Migration Plan

Land as a single feature commit (implementation + tests + tasks checkbox flip): no data migration beyond the natural cache miss. Rollback = revert commit; cache entries written post-change carry `"XPSNR"` and simply miss again pre-change (harmless).

## Open Questions

None blocking. Anime-specific bucketing is deliberately deferred until there is evidence it matters for actual batches.
