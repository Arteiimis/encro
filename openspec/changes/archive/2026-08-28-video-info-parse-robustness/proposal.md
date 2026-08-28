## Why

`src/video/video_info.cpp`'s `tryParseNbFrames` calls `std::stoll` on the `nb_frames` field of a probe JSON without a guard, so a non-numeric string (e.g. `"abc"`) throws `std::invalid_argument` out of `getVidTotalFrames` despite its `eh::Result<int64_t>` contract. In production the exception is only contained by `runEncodingTask`'s catch-all, which misattribates it as a task failure; in unit tests the throw escapes entirely. Every other numeric parse in the module (`parseDouble`, `parseFraction`) already returns `nullopt` instead of throwing — `tryParseNbFrames` is the one holdout.

## What Changes

- `src/video/video_info.cpp`: guard `tryParseNbFrames` so unparseable `nb_frames` strings (anything that is not a canonical integer) yield `std::nullopt` — the same no-throw semantics as `parseDouble`/`parseFraction`. The existing fallback chain in `getVidTotalFrames` (duration × frame-rate, then error) then handles the value exactly as it handles a missing `nb_frames` today.
- Specs: new `video-info-parse` capability documenting the module-wide no-throw contract for probe JSON numeric fields: arbitrary string values never throw out of the public `getVidTotalFrames` / `getVidTotalDurationUs` surfaces.
- Tests: extend `tests/video_info_tests.cpp`'s cache-seeded `getVidTotalFrames` SECTION table with the previously out-of-scope corner — an arbitrary non-numeric `nb_frames` value falls back to the rate estimate (or errors) instead of throwing.
- The `runEncodingTask` catch-all stays as-is; defense-in-depth is not removed by this change.

## Impact

- `src/video/video_info.cpp` — small guarded change (1 function).
- `tests/video_info_tests.cpp` — new SECTIONs.
- No CLI, config, or file-format changes; no new env knobs.

## Alternatives Considered

- Wrapping only in `try/catch` inside `tryParseNbFrames` — chosen (matches the existing `parseDouble` style, 2-line diff).
- `std::from_chars` for stricter integer parsing — rejected as over-scoped for a defensive guard; `stoll` in a `try` block preserves the existing accepted formats (optional sign, base-10, `int64` range) exactly.
- Leaving it to the catch-all — rejected: it converts a mis-parse into a hard task failure and hides the field's intended fallback semantics.