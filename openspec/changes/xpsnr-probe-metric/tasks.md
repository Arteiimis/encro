## 1. Metric plumbing in videoquality

- [ ] 1.1 Add `QualityMetric::Xpsnr` and TDD unit tests: `parseXpsnrStats` (well-formed per-plane lines, plane-mean per frame, truncated/empty file errors, negative dB values)
- [ ] 1.2 Implement `runXpsnr` (xpsnr filter via stats_file, same trim→scale2ref chain) + parser; wire as first attempt for non-HDR in `measureSegmentQuality`, VMAF second, SSIM last, each fallback warning naming the unavailable metric
- [ ] 1.3 Replace `ssimFloorForVmafFloor` usage with anchor-mapping helper covering all three metrics (`meetsFloor`/`floorForMetric` in encode_probe route through it); XPSNR anchors `{90→38.5, 95→41.0, 97→42.5}` with interpolation/clamp + calibration-provenance comment
- [ ] 1.4 Unit tests for the floor mapping: exact anchors, interpolation between anchors, clamping below/above range

## 2. Decision path, cache, and output formatting

- [ ] 2.1 `encode_probe.cpp`: metric labels and HDR cache-key metric selection recognize `"XPSNR"`; verify `meetsFloor` routing for Xpsnr points with tests; plan-header label fixed to VMAF-scale wording instead of `plans.front().metric` (test: mixed-metric batch header)
- [ ] 2.2 Probe-point metric-consistency guard (design D7): windows disagreeing on metric discard the point with a warning and follow the probe-failure path (unit test: A=XPSNR B=VMAF → nullopt, warn logged)
- [ ] 2.3 Probe cache round-trip test with `"XPSNR"` entries; update probe_cache.h comment; confirm old `"VMAF"` keys miss after upgrade (regression test on key construction)
- [ ] 2.4 `formatProbeP5` + preview score formatting: signed dB rendering with explicit unit for XPSNR; mixed-metric plan rows render per-row units (unit tests)
- [ ] 2.5 Preview filtergraph/metric-label branches updated to the three-way chain (no behavioral change beyond labels)

## 3. E2E and regression

- [ ] 3.1 Extend fake_media_tool xpsnr impersonation (stats-file mode, env-switchable); e2e: primary XPSNR scoring picks expected CQ, and forced-failure transitions exercise XPSNR→VMAF→SSIM warnings
- [ ] 3.2 Full suites green: `xmake test-report` summary clean; real-ffmpeg smoke over one sample shows p5 dB labeled rows in plan and preview outputs

## 4. Wrap-up

- [ ] 4.1 `xmake fmt` + `xmake tidy` report check; mark tasks complete; post-change code-review skill run against spec deltas
