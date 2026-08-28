# Design: video-info-parse robustness

## Context

See proposal.md — Why. The offending call is `std::stoll(text)` in `tryParseNbFrames` (src/video/video_info.cpp ~:94); every sibling parser (`parseDouble`, `parseFraction`) is already `try/catch`-guarded and returns `std::nullopt`. `getVidTotalFrames` treats a `nullopt` from `tryParseNbFrames` identically to a missing field: it falls through to duration × `avg_frame_rate` (or `r_frame_rate`), then errors. So the fix needs no fallback-chain changes — only the guard.

## Goals / Non-Goals

Goals:
- `tryParseNbFrames` never throws; unparseable strings return `nullopt`.
- A canonical regression test for the previously out-of-scope corner (non-numeric `nb_frames`).

Non-goals:
- No change to the accepted formats of `stoll` (optional sign, base-10, in-int64-range stays parseable); no `std::from_chars` rewrite.
- No change to `runEncodingTask`'s catch-all (defense-in-depth stays).
- No restructuring of the fallback chain.

## Approach

1. `src/video/video_info.cpp` — `tryParseNbFrames`: wrap the `stoll` call the same way `parseDouble` wraps `stod`:

   ```cpp
   if (val.is_string()) {
     auto const text = std::string{val.as_string()};
     if (!text.empty() && text != "N/A") {
       try { return std::stoll(text); } catch (...) { return std::nullopt; }
     }
   }
   ```

   Two-line diff, style-identical to the sibling parsers. `jsonValToString` and the int64/uint64 branches are unaffected.

2. `tests/video_info_tests.cpp` — extend the cache-seeded `getVidTotalFrames` SECTION table (scaffold already exists from orchestration-unit-tests): one SECTION seeding `nb_frames: "abc"` with parseable `avg_frame_rate` + `duration`, asserting the duration×rate estimate and no throw; one SECTION with `nb_frames: "abc"` and no usable rate/duration, asserting an error result. Both vanish if the guard is reverted (exception → test crash).

## Risks

- `stoll` on very long digit strings can throw `std::out_of_range` — also caught by `catch (...)`; parses that previously threw now degrade to fallback, which is the intended contract.
- No ABI/CLI impact; no new dependencies.

## Verification

- `xmake test-report --tag="[video-info]"` green, then `xmake test-parallel` green.
- Coverage: unchanged scope (guard path is exercised by the new SECTIONs; the revert-check is the throw crash).