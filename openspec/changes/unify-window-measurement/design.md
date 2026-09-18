## Context

See `proposal.md` — Why for the pain. The trigger: probing is the decision layer for every MP4 encode (it produces the plan shown before the confirmation prompt) and preview shares it, so a change to scoring has to be made twice. The state that shapes the approach:

- probe's sequence (`src/video/encode_probe.cpp:56-140`): `runProbeEncode` per window → `videoInfoCache.find(inputPath).value_or({})` → `measureSegmentQuality(QualityRequest{…, encodedHasLocalPts = true})` per window → metric-agreement check → pool `frameScores` → `percentile(pool, 5.0)` → `ProbePoint{cq, p5, metric, bytes}` where `bytes` is both segments' size.
- preview's sequence (`src/preview/preview_process.cpp:370-423`): `runProbeEncode` → `videoInfoCache.find(original).value_or({})` → the same `QualityRequest` → `percentile(scores->frameScores, 5.0)` → `WindowOutcome{metric, score}`, with the batch flag `windowEncodeFailed` set only when the **encode** fails.
- Shared pieces that already exist: `runProbeEncode` (`encode_probe.h:96-104`), `ProbeWindow{startUs, durationUs}` (`encode_probe.h:28-31`), `EncodeInputSettings` and `EncodeProfile` (`encode_config.h`), `videoquality::QualityRequest` / `SegmentScores` / `percentile` (`video_quality.h:20-40`), `measureSegmentQuality` (`:72`).
- Cache: `appctx::RuntimeContext::VideoInfoCacheStore` (`app_context.h:103-123`, `shared_mutex`-guarded, `set`/`find`/`size`), written by `video_info.cpp:173,209,255` and `preview_process.cpp:175`; the whole-json cache-aware read is file-local (`video_info.cpp:163`), so the five external sites read the map by hand (`encode_probe.cpp:82,205,620`, `preview_process.cpp:399,722`).
- Test harness: `tests/video/encode_probe_tests.cpp:385-440` (`fillProbeContext`, `EncodingTasksScaffold`) builds ffmpeg/ffprobe fakes from `FAKE_TOOL_EXE_PATH` with `ScopedEnvVar` and sets `ENCRO_FAKE_FFMPEG_WRITE_VMAF`/`ENCRO_FAKE_FFMPEG_VMAF_SCORES`; `ENCRO_FAKE_TOOL_LOG_FILE` is set by `EncodingTasksScaffold` (`:437`) and the scoring-failure knobs `ENCRO_FAKE_FFMPEG_SCORING_FAIL_MATCH/_UNLESS` (argv-text matching, `fake_media_tool.cpp:347-359`) are already used at `:960,993-996,1023`.

## Goals / Non-Goals

**Goals:**

- One place constructs "how a probe window is measured": the encode call, the metadata lookup, the `QualityRequest`, the scoring-failure branch, the encoded byte count, and the step report.
- One module owns the in-process metadata cache: `video_info` is its only reader and writer, and the other modules ask it for the whole json instead of reaching into the map.
- The two callers keep the policies that are theirs: probe's metric-agreement discard and two-window pooling, preview's default-outcome-on-scoring-failure and batch flag.

**Non-Goals:**

- **No constant or policy merging.** `pickProbeWindows` / `pickPreviewWindows` and the three 10 s window constants (`kSegmentDurationUs`, `kProbeWindowDurationUs`, `kWindowDurationUs`) stay separate — `reduce-over-engineering` recorded them as distinct domain constants that happen to be equal.
- **No pre-reduction in the seam.** Returning a per-window p5 would change probe's chosen CQ, because `p5(pooled frames) != p5(two p5s)`.
- **The cache object does not move** out of `RuntimeContext`; after this change a single module owns it, and moving it would touch `app_context.h` and every runtime consumer for no additional guarantee.
- **No new files**: the seam lives next to `runProbeEncode`, the cases next to the existing harness.
- **No behavior change** in probe output, chosen CQ, preview windows, scores, or the comparison video.

## Decisions

**D1 — The seam lives in `encode_probe.{h,cpp}` (namespace `encodeprobe`).**
It composes `runProbeEncode`, which already lives there; preview already includes this header for `runProbeEncode` and `ProbeWindow`, so no new dependency or layering is introduced.
*Alternatives:* a new `window_measure.{h,cpp}` names the shared seam more loudly, but costs a file and an include for ~40 lines while preview keeps depending on `encode_probe.h` either way; leaving the duplication alone was rejected by the architecture review.

**D2 — The payload is raw, callers reduce.**

```cpp
struct WindowMeasurement {
  videoquality::QualityMetric metric;
  std::vector<double> frameScores;  // as scored; the caller owns the reduction
  std::uint64_t bytes = 0;          // size of the encoded segment this call produced
};

struct WindowMeasureRequest {
  fs::path inputPath;
  fs::path segFile;
  ProbeWindow window;
  int cq = 0;
  std::size_t workerCount = 0;
  EncodeInputSettings settings;
  // Fired before each step this seam performs: "encode", then "score".
  // Optional; the caller owns the per-window label (D8).
  std::function<void(std::string_view phase)> onStep = {};
};

auto measureWindow(appctx::AppContext& ctx, WindowMeasureRequest const& request)
  -> eh::Result<std::optional<WindowMeasurement>>;
```

The seam guards every call (`if (request.onStep) …`) — `measurePoint`'s default is a deliberately guarded `onStep = {}` (`encode_probe.cpp:64`), and calling an empty `std::function` throws.

`bytes` rides along because the seam just wrote that file; probe sums it over its two windows and preview ignores it.

**D3 — Failure has two levels: `eh::Result<std::optional<WindowMeasurement>>`.**
Outer error = the encode failed (preview turns this into `windowEncodeFailed` plus an error, as today); inner `nullopt` = scoring failed (preview returns the default `WindowOutcome`, as today). The repo already uses this shape for a two-level outcome (`ensureAudioFile`, `video_encode_runner.cpp:486`).
*Alternatives:* a `{EncodeFailed, ScoreFailed, Ok}` enum is more explicit but introduces a second error vocabulary for two callers that already speak `eh::Result`; `Result<WindowMeasurement>` with an inner optional member means a new wrapper type carrying only the two fields that are already meaningless when scoring fails.

**D4 — `ProbeWindow` is reused as the request's window field.**
Note the one thing this does *not* remove: preview still builds `ProbeWindow{window.startUs, window.durationUs}` from its own `Window` (which also carries `score`/`metric` for reporting) — the conversion moves into the request initializer rather than disappearing. Reusing the existing type is preferred over introducing a third window shape.
*Alternative:* give the request `startUs`/`durationUs` fields directly; rejected because it drops the shared window type for no gain.

**D5 — Cache: export the whole-json cache-aware read, then route every raw read through it.**
`videoinfo::cachedVidInfo(toolchain, runtime, path) -> boost::json::value` is the public form of the file-local `loadCachedOrProbeVideoInfo` (`video_info.cpp:163`); it returns the cached json when present, otherwise probes once and caches. `video_info.h` currently declares free functions, so the exported reader introduces the module's `videoinfo` namespace. Callers: the new seam (`originalVideoInfo`), `preview::probeVideo` (replacing `getVidInfo` + `videoInfoCache.set`), and the remaining raw reads (`encode_probe.cpp:205,620`, `preview_process.cpp:722`).
*Alternatives:* fixing only preview's write leaves three raw reads repeating `.value_or(boost::json::value{})` and two modules touching the map; moving the cache out of `RuntimeContext` is the larger refactor the Non-Goals reject.
*Side effect:* preview no longer re-probes a cached path — one fewer ffprobe subprocess per cached input. Values are unchanged (same file, same tool, one process run); the cache is process-scoped and every entry came from this same probe path.

**D6 — Policies stay with their owners.**
Probe keeps the metric-agreement discard and the byte sum; preview keeps its failure policy; window selection stays in the two `pick*Windows` functions; `runProbeEncode` keeps its signature (the seam calls it as-is, so no churn in its tests).

**D7 — Four named test cases in `tests/video/encode_probe_tests.cpp` (tag `[encode-probe]`).**

1. the seam returns the encoded segment's raw frame scores, its metric, and its byte count (fake VMAF scores via `ENCRO_FAKE_FFMPEG_VMAF_SCORES`), and reports its two steps through the request's `onStep` (`"encode"` then `"score"`, D8);
2. failure semantics: a scoring failure surfaces as inner `nullopt` (`ENCRO_FAKE_FFMPEG_SCORING_FAIL_MATCH`), an encode failure as the outer error (non-zero `ENCRO_FAKE_FFMPEG_EXIT_CODE`);
3. the seam hands back each window's own metric under a mixed-metric split (no agreement policy in the seam): with `WRITE_VMAF=1`, `WRITE_XPSNR=1`, `SCORING_FAIL_MATCH="_0.ts"` and `SCORING_FAIL_UNLESS="stats_file="`, the first window's VMAF attempt fails while its XPSNR fallback scores, so one call returns `Xpsnr` and the other `Vmaf` — without the `_UNLESS` all three providers match the segment name and fail. The discard that follows (mixed metrics ⇒ point dropped) is already pinned end-to-end by the existing case `runProbePhase discards probe points whose windows mix metrics`, which stays in place and must stay green; no duplicate phase-level case is added;
4. preview reuses a warm cache entry: one ffprobe invocation for a path that was already probed, counted from `ENCRO_FAKE_TOOL_LOG_FILE`. Since `preview::probeVideo` is file-local, the case warms the cache through `videoinfo::cachedVidInfo` (or a first run) and drives `preview::run`, setting `ENCRO_FAKE_TOOL_LOG_FILE` itself — the shared `fillProbeContext` helper does not.

**D8 — The request carries an optional step hook so the four per-point progress phases survive.**
`kStepsPerProbePoint = std::size_t{4}` (`encode_probe.h:69`) is the unit both bars count: probe's `onStep` advances `100 * step / kMaxProbeSteps` (`encode_probe.cpp:538-541`) and its `onPoint` jumps to `done * kStepsPerProbePoint / kMaxProbeSteps` (`:552`); preview counts the same way at `40 * step / kMaxProbeSteps` (`preview_process.cpp:557-561`, `:572`) because it narrates the shared probe through `probeSingleFile` (`:579`). Today `measurePoint` fires one `onStep` before each of its four steps (`encode_probe.cpp:71,75,83,95`), so a seam that performs two of those steps (one encode, one score) without reporting them leaves both bars at the previous point's fraction until the point completes — and `measurePoint` cannot emit the score phase itself, because it fires after the seam's encode and before its scoring, both inside the seam. The request's hook is therefore phase-only (`"encode"`, `"score"` — the cq is already the request's), and `measurePoint` labels each phase with its window index (`"encode 1/2"`, `"score 1/2"`, then `2/2`): the same four labels and fractions per point as today, but emitted per window (`encode 1/2`, `score 1/2`, `encode 2/2`, `score 2/2`) instead of both encodes preceding both scores. A caller that omits the hook (tests, pure callers) sees today's `onStep = {}` default. On a scoring failure today all four steps have already fired (`encode_probe.cpp:83,95` precede the `nullopt` check at `:98-101`); after the seam, the aborted window reports two. Nothing pins that count on the failure path — the point is discarded either way and the plan falls back identically — so this is recorded rather than defended.

## Risks / Trade-offs

- [The seam silently pre-reduces and probe picks a different CQ] → it returns raw `frameScores` (D2); case 1 plus the existing `probeCqSequence` cases pin the reduction.
- [Metric agreement is silently dropped when `measurePoint` delegates] → the existing phase-level case (`runProbePhase discards probe points whose windows mix metrics`) pins the discard end-to-end, and D7.3 pins the seam handing back per-window metrics without imposing agreement.
- [The seam silently drops the four per-point progress phases, so probe and preview bars stall between points] → `WindowMeasureRequest::onStep` reports the seam's two steps (D8), `measurePoint` re-labels them per window, and D7.1 pins the seam's two emissions, so `kStepsPerProbePoint = 4` stays true (the labels and fractions are unchanged; only their order becomes per-window).
- [Cache reuse hides a genuine need to re-probe] → the value is process-scoped, path-keyed, and produced by the same ffprobe path; preview's previous behavior differed only in cost.
- [Two window concepts remain (`ProbeWindow`, preview's `Window`)] → accepted; the request reuses `ProbeWindow` so no third shape is introduced, and the conversion is one initializer.
- [Payload growth] → `WindowMeasurement` is a struct; a field one caller ignores (`bytes` for preview) costs nothing and future scoring fields land in one place.

## Migration Plan

None: internal only, no persisted format, CLI surface, or spec-level behavior changes. Rollback is a revert of the single refactor commit.
