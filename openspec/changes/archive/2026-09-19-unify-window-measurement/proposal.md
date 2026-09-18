## Why

The same measurement — encode a 10 s window at a CQ, score it against the original, reduce the frame scores — is written twice: probe's `measurePoint` (`src/video/encode_probe.cpp:56-140`) and preview's `encodeAndScoreWindow` (`src/preview/preview_process.cpp:370-423`) build the identical `videoquality::QualityRequest` yet reduce differently (probe pools two windows' frames into one percentile, preview takes p5 over a single window), and the divergence is invisible until someone edits one of them. Separately, the in-process metadata cache (`appctx::RuntimeContext::VideoInfoCacheStore`) has two module owners: `video_info.cpp` owns the cache-aware read (`loadCachedOrProbeVideoInfo`, `:163` — file-local, so no other module can use it) while `preview_process.cpp:175` also writes it and reads through `getVidInfo`, bypassing the cache, so the five sites that need the whole json (`encode_probe.cpp:82,205,620`, `preview_process.cpp:399,722`) each reach into the map by hand.

## What Changes

- **New seam** `measureWindow` in `src/video/encode_probe.{h,cpp}` (namespace `encodeprobe`, beside the `runProbeEncode` it composes — preview already includes this header and uses `encodeprobe::ProbeWindow`):
  - `struct WindowMeasurement { videoquality::QualityMetric metric; std::vector<double> frameScores; std::uint64_t bytes; }`
  - `auto measureWindow(appctx::AppContext& ctx, WindowMeasureRequest const& request) -> eh::Result<std::optional<WindowMeasurement>>` — outer error = the window encode failed; inner `nullopt` = scoring failed.
  - `WindowMeasureRequest{inputPath, segFile, ProbeWindow window, cq, workerCount, EncodeInputSettings settings, onStep}` reuses the existing `ProbeWindow`; `onStep` is the optional phase hook the seam fires before its encode and before its scoring (`"encode"`, `"score"`) — `measurePoint` labels it per window so both bars keep counting four `onStep` calls per point, as they do today. (Preview still builds it from its own `Window`, which carries the reporting fields the request does not need — that one initializer is unchanged, not eliminated.)
  - The seam does **not** reduce to a percentile: probe pools both windows' `frameScores` and takes one p5 over the pool, preview takes p5 over its own window, and `p5(pooled) != p5(two p5s)`. Reduction stays with each caller — that is a different aggregate, not a duplicate.
- **Both call sites delegate**: `measurePoint` calls the seam twice and keeps its metric-agreement rule (discard the point), its byte sum, and its four per-point `onStep` emissions (relayed through the request hook); `encodeAndScoreWindow` calls it once and keeps its failure policy (encode failure flags `windowEncodeFailed`, scoring failure returns the default `WindowOutcome`).
- **The metadata cache gets one owner**: export video_info's whole-json cache-aware read as `videoinfo::cachedVidInfo(toolchain, runtime, path)` — today's file-local `loadCachedOrProbeVideoInfo` (`video_info.cpp:163`), which already backs `getVidTotalFrames`/`getVidTotalDurationUs`/`getVidDimensions`/`getVidHasAudio` — and route preview's `probeVideo` and the remaining external raw reads (`encode_probe.cpp:205,620`, `preview_process.cpp:722`) through it. `preview_process.cpp:175`'s `runtime.videoInfoCache.set(...)` is deleted.
- **Observable side effect, not a behavior change**: preview stops re-probing a path that is already cached, so a cached preview input costs one fewer ffprobe subprocess. The value is unchanged — same file, same tool, same process run.
- **Tests**: four named cases in `tests/video/encode_probe_tests.cpp` (which already hosts the fake-tool harness): the seam's payload (raw frame scores + bytes), the seam's failure semantics (scoring failure = inner `nullopt`, encode failure = outer error), the seam handing back each window's own metric under a mixed-metric split (probe's agreement discard is already pinned end-to-end by the existing `runProbePhase discards probe points whose windows mix metrics` case), and preview's warm-cache reuse asserted by ffprobe invocation count (`ENCRO_FAKE_TOOL_LOG_FILE`, driven through `preview::run` because `probeVideo` is file-local).

No user-visible change: the probe plan, the chosen CQ, preview windows, scores, and the rendered comparison video are identical; both bars still advance through the same four phases per point (`encode n/2`, `score n/2`) to the same fractions, now interleaved per window instead of both encodes preceding both scores.

## Capabilities

### New Capabilities

None — the change moves an existing measurement behind one interface; it introduces no observable behavior.

### Modified Capabilities

None — `video-encode-probing` and `video-preview` specify window selection, scoring, and plan output, none of which change. The in-process metadata cache and ffprobe call counts are not spec surfaces: the caches that `openspec/specs/` does specify — organize's analysis cache, picture-compression's output cache, `video-encode-probing`'s persisted probe-decision cache — are persisted, path- or settings-keyed mechanisms with different lifetimes, not this process-scoped json store. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `refactor-long-param-lists` / `remove-immer-simplify-locks` / `reduce-over-engineering`.

## Impact

- `src/video/encode_probe.{h,cpp}` — the seam, `measurePoint` delegating, two raw cache reads routed
- `src/preview/preview_process.cpp` — `encodeAndScoreWindow` delegating, `probeVideo` routed through the cache-aware read, its cache write deleted, two raw cache reads routed
- `src/video/video_info.{h,cpp}` — the file-local helper becomes the public whole-json reader; its four existing callers in `video_info.cpp` move to the exported name
- `tests/video/encode_probe_tests.cpp` — four new cases (no new test file; the fake-tool harness lives here)
- No new dependencies, no new files, no user-facing surface touched.

**Explicitly out of scope:**

- Window selection policy (`pickProbeWindows`, `pickPreviewWindows`) and the three `10'000'000` µs constants stay as they are — `reduce-over-engineering` recorded them as distinct domain constants that happen to be equal.
- `RuntimeContext::VideoInfoCacheStore` stays a field of `RuntimeContext`: it is already thread-safe, and after this change `video_info` is its only reader and writer, so moving it out buys no additional guarantee.
