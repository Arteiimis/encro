## 1. New module (TDD: tests first)

- [x] 1.1 Add `src/video/segment_plan.h` declaring `SegmentPlan`, `planSegments`, `closedSegments`, `segmentMarkUs` (design D1) and verify each of the four is present (`rg -q "struct SegmentPlan" src/video/segment_plan.h && rg -q "planSegments" src/video/segment_plan.h && rg -q "closedSegments" src/video/segment_plan.h && rg -q "segmentMarkUs" src/video/segment_plan.h`)
- [x] 1.2 Add `tests/video/segment_plan_tests.cpp` (tag `[segment-plan]`) with the four named cases from design D7 — gap inside the prefix stops it, list reaches the timeline end with fewer segments than the duration implies, in-flight segment leaves the task incomplete, frame-offset conversion (including `totalFrames == 0`) — and verify `xmake test-report --tag="[segment-plan]"` fails before the implementation exists (red first)
- [x] 1.3 Implement `segmentMarkUs` and `closedSegments` in `src/video/segment_plan.cpp`, reusing `parseSegmentList`; verify the exit-direction cases pass
- [x] 1.4 Implement `planSegments` — directory walk via `fs::exists` on `segmentFileName(index)`, the recorded-count-authority prefix rule, `segmentTotal = ceil(duration / kSegmentDurationUs)`, the list-reaches-timeline completion rule, and `baseFrameOffset` via `segmentBaseFrameOffset`; verify all `[segment-plan]` cases pass
- [x] 1.5 Verify the block is a pure move: `rg -n "reusableSegments|encodeComplete|segmentBaseFrameOffset|kSegmentDurationUs" src/video` shows each rule defined once, and the module contains no `exec`, `Store`, or `store` reference

## 2. Migrate the entry direction (runner)

- [x] 2.1 Replace the inline derivation in `runSegmentedEncoding` (`video_encode_runner.cpp:612-671`) with one `planSegments` call, keeping the store read and segment-dir setup (`:603-610`), audio extraction and assembly in the runner (design D3); delete the local `reusableSegments` and `encodeComplete`; verify `xmake test-report` is green and the deleted symbols have no remaining callers (`rg`)
- [x] 2.2 Build `SegmentSeries` from `plan.startNumber()` / `plan.resumeUs` (design D2) and verify the generated command line is unchanged: `xmake build e2e_tests && xmake run e2e_tests` stays green, including the `-ss` / `-segment_start_number` cases
- [x] 2.3 Route the concat-only path (`plan.complete`) and the assembly input (`plan.reusableNames`) through the plan; verify the "interrupted during concat" e2e case still assembles without re-encoding

## 3. Migrate the exit direction (watcher)

- [x] 3.1 Replace the parse-and-multiply body of `markCompletedSegments` (`video_encode_runner.cpp:379-391`) and the post-run assembly-input read (`:739`) with `closedSegments` + `segmentMarkUs`, keeping the monotonic guard, the `std::atomic`, and the `Store::markSegmentProgress` call in the runner (design D5); verify the e2e resume cases and the job-state `segmentIndex` assertions (`encro_e2e_tests.cpp:1394,1418,1469`) pass, and that no production `parseSegmentList` call remains outside `src/video/segment_plan.cpp` (`rg`)

## 4. Unsync fix

- [x] 4.1 Snapshot `baseFrameOffset` and `totalFrames` under `state.mtx` in `getEncodingProgress` (`video_encoding_state.cpp:113-117`), alongside the existing `progressFilePath` snapshot; verify `rg -n "baseFrameOffset" src/video` shows the read inside the lock scope and `xmake test-report` is green

## 5. Verification & commits

- [x] 5.1 Run `xmake test-report` (full unit suite) and confirm zero failures, no new assertion counts lost
- [ ] 5.2 Run `xmake test-parallel` and confirm every shard reports its assigned case count
- [x] 5.3 Run `xmake fmt` before committing; the pre-commit hook re-checks staged C++ files
- [x] 5.4 Commit the planning artifacts first as their own `docs:` commit (proposal, design, tasks, `.openspec.yaml`), then implementation + tests + ticked `tasks.md` in one `refactor:` commit (English, conventional, subject < 72 chars, body wrapped at 80)
