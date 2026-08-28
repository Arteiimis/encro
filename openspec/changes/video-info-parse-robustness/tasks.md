# Tasks: video-info-parse-robustness

## 1. Guard the nb_frames parse

- [x] 1.1 Guard `tryParseNbFrames` in `src/video/video_info.cpp`: wrap the `std::stoll` call in `try/catch (...)` returning `std::nullopt`, matching the existing `parseDouble`/`parseFraction` no-throw style.
- [x] 1.2 Add two cache-seeded SECTIONs to the `getVidTotalFrames` test in `tests/video_info_tests.cpp` (same scaffold as the orchestration-unit-tests corners):
  - non-numeric `nb_frames: "abc"` + parseable `avg_frame_rate` + `format.duration` → returns the duration×rate estimate, does not throw;
  - non-numeric `nb_frames: "abc"` with no usable rate/duration → returns an error result, does not throw.
- [x] 1.3 Run `xmake test-report --tag="[video-info]"`; confirm green (the new SECTIONs fail with a crash if 1.1 is reverted).
- [x] 1.4 Run `xmake test-parallel`; confirm all shards green.

## 2. Final verification

- [x] 2.1 Run `openspec validate video-info-parse-robustness`; confirm valid.