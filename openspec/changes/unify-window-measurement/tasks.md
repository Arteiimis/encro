## 1. Export the whole-json cache-aware read (video_info)

- [x] 1.1 Promote the file-local `loadCachedOrProbeVideoInfo` (`video_info.cpp:163`) to a public `videoinfo::cachedVidInfo(toolchain, runtime, path)` declared in `video_info.h` (introducing the `videoinfo` namespace the module currently lacks); verify its four existing callers in `video_info.cpp` (`:307,396,418,449`) use the new name and the existing video_info unit cases pass
- [x] 1.2 Verify the exported reader is the only cache-aware read: `rg -n "loadCachedOrProbeVideoInfo" src` returns nothing and `xmake build encro` compiles the four migrated callers against the new name

## 2. The measurement seam (TDD: tests first)

- [x] 2.1 Add the four cases from design D7 to `tests/video/encode_probe_tests.cpp` (tag `[encode-probe]`, reusing the existing fake-tool harness; case 4 drives `preview::run`) and verify `xmake test-report --tag="[encode-probe]"` fails before the seam exists (red first)
- [x] 2.2 Declare `WindowMeasurement`, `WindowMeasureRequest` and `measureWindow` in `src/video/encode_probe.h` (design D2/D3/D8) and verify the header compiles with `xmake build encro`
- [x] 2.3 Implement `measureWindow`: `runProbeEncode` → `videoinfo::cachedVidInfo` for `QualityRequest.originalVideoInfo` → `measureSegmentQuality` with `encodedHasLocalPts = true` → segment byte count, firing the request's `onStep` before the encode and before the scoring (D8, `"encode"` then `"score"`); verify cases 1–3 pass from `--tag="[encode-probe]"` (case 4 is verified in 4.3)

## 3. Delegate the two call sites

- [x] 3.1 `measurePoint` (`encode_probe.cpp:56-140`) calls the seam twice, keeps the metric-agreement check and the byte sum, and relays the seam's two phases per window as the four labels the bars already expect (`encode 1/2`/`score 1/2`, then `encode 2/2`/`score 2/2` — interleaved per window, where today both encodes precede both scores; D8); verify the existing `probeCqSequence` and `runProbePhase discards probe points whose windows mix metrics` cases pass, D7 cases 1–3 pass, and the relays still total four `onStep` calls per point (both bars' `done * kStepsPerProbePoint` math assumes it). The full suite is only green once 4.1 lands (case 4 stays red until then), so it is checked in 5.1
- [x] 3.2 `encodeAndScoreWindow` (`preview_process.cpp:370-423`) calls the seam once, keeps `windowEncodeFailed` on encode failure and the default `WindowOutcome` on scoring failure; verify the `[preview]` cases pass

## 4. Cache: one owner

- [x] 4.1 `preview::probeVideo` (`preview_process.cpp:169-201`) reads through `videoinfo::cachedVidInfo` and drops its `runtime.videoInfoCache.set`; verify `probeVideo` no longer calls `getVidInfo` or `videoInfoCache.set`
- [x] 4.2 Route the remaining raw reads (`encode_probe.cpp:205,620`, `preview_process.cpp:722`) through the exported reader; verify `rg -n "videoInfoCache" src` lists only `video_info.cpp` and `app_context.h`, no site repeats `value_or(boost::json::value{})`, and the affected unit cases pass
- [x] 4.3 Verify the warm-cache case from D7.4 is green in `tests/video/encode_probe_tests.cpp`: one ffprobe invocation for an already-probed path, counted from `ENCRO_FAKE_TOOL_LOG_FILE`

## 5. Verification & commits

- [x] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures
- [x] 5.2 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 5.3 Commit the planning artifacts first as their own `docs:` commit, then implementation + tests + the ticked `tasks.md` in one `refactor:` commit; verify with `git log --oneline -2` that the split is docs-then-refactor
