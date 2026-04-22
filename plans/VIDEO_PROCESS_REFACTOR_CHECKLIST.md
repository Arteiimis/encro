# Video Process Refactor Checklist

This checklist is the working plan for refactoring the video processing module without changing existing behavior.

## Goals

- Reduce code volume in `src/video/video_process.cpp` by separating unrelated responsibilities.
- Remove duplicate control flow between single-path and multi-file entry points.
- Improve readability by flattening nested branches and shortening long functions.
- Keep public behavior, output paths, job-state semantics, and packing behavior unchanged.

## Non-Goals

- Do not redesign the concurrency model.
- Do not change CLI options, output naming rules, or state-file behavior.
- Do not add new abstractions unless they remove duplication immediately.
- Do not rewrite tested pure logic unless it is being extracted with identical behavior.

## Current Constraints

- `src/video/video_process.cpp` currently mixes path planning, progress parsing, ffmpeg diagnostics, encoding execution, progress UI, resumable task state, packing, and top-level orchestration.
- Existing tests mostly protect pure logic in `tests/video_process_tests.cpp`.
- Orchestration-level behavior has little direct test coverage and must be changed more carefully.

## Execution Order

### Phase 0: Guard Rails

- [x] Add orchestration-focused tests before moving control flow.
- [x] Cover at least: single path happy path, multi-file validation failure, interrupted run handling, optional pack branch.
- [x] Record the narrowest command that builds and runs the affected tests.

Verified on Windows with:

- `xmake build tests; .\build\windows\x64\release\tests.exe "[video-process][orchestration]"`

Exit criteria:

- Refactor-sensitive behavior has tests outside the pure helper coverage already present.

### Phase 1: Extract Pure Planning Logic

- [ ] Move output-path planning helpers out of `src/video/video_process.cpp` into a dedicated internal module.
- [ ] Move pack grouping helpers out of `src/video/video_process.cpp` into a dedicated internal module or pack-facing helper.
- [ ] Keep existing public functions stable unless there is a clear reason to narrow the header surface later.
- [ ] Re-run existing path-planning and pack-grouping tests after extraction.

Target logic:

- `planVideoOutputFiles`
- `resolveVideoOutputPath`
- `resolveVideoPackOutputPath`
- `groupEncodedVideosForPack`
- related naming and duplicate-resolution helpers

Exit criteria:

- Path planning and pack grouping no longer live in the main orchestration file.
- Existing unit tests pass without behavior changes.

### Phase 2: Extract Progress and ffmpeg Parsing

- [ ] Move progress-file parsing and ffmpeg line classification into a focused internal module.
- [ ] Keep the parser API small and value-based.
- [ ] Keep tests centered on pure functions.

Target logic:

- `readLastNLines`
- `parseProgressFile`
- `isLikelyFfmpegErrorLine`
- related string helpers used only for parsing/classification

Exit criteria:

- Text parsing and progress parsing are isolated from orchestration code.
- Existing parser tests still pass.

### Phase 3: Unify Entry Orchestration

- [ ] Introduce one internal workflow for: scan -> validate -> plan outputs -> prepare actions -> run encoding -> handle stop -> maybe pack -> summarize.
- [ ] Keep `handlePathEncoding` and `handleMultiFileEncoding` as thin wrappers.
- [ ] Pull duplicated stop-handling and result-merging code into shared helpers.
- [ ] Prefer early returns over nested conditionals.

Target duplication:

- repeated planning, prepare, run, merge, stop, pack, and summary logic in the two entry functions

Exit criteria:

- The two public entry points differ only in input acquisition and their unique preconditions.
- Main flow reads top-down without large repeated blocks.

### Phase 4: Extract Encoding Execution

- [ ] Move `encodeToHevc` support logic into a dedicated execution module.
- [ ] Separate state preparation, config construction, command execution, and WebP adaptive retry logic.
- [ ] Keep `encodeToHevc` as the stable facade unless callers can be updated cheaply.

Target logic:

- `encodeToHevc`
- WebP adaptive encode helpers
- config preparation and status update bridging

Exit criteria:

- Encoding command execution can be read independently from orchestration and progress UI.
- WebP-specific logic is no longer interleaved with generic encoding flow.

### Phase 5: Extract Batch Execution and Progress UI

- [ ] Move progress state structs and monitor logic into a batch-execution module.
- [ ] Isolate slot management, progress bars, active-state tracking, and task lifecycle updates.
- [ ] Keep thread behavior unchanged in the first pass.
- [ ] Simplify long functions only after extraction preserves behavior.

Target logic:

- `EncodingProgressState`
- `EncodingExecutionContext`
- `startEncodingMonitor`
- `runEncodingTask`
- `runEncodingTasks`

Exit criteria:

- The main video orchestration file no longer owns thread and progress implementation details.
- Progress behavior remains unchanged in manual and automated checks.

### Phase 6: Remove Cross-Module Duplication

- [ ] Deduplicate resumable pack execution shared with `src/app/pipeline.cpp`.
- [ ] Move reusable pack execution logic to the pack/app layer instead of keeping a video-only copy.
- [ ] Narrow `src/video/video_process.h` so it exposes only stable entry points and intentionally public helpers.

Exit criteria:

- Shared pack execution code exists in one place.
- Public header surface is smaller and more intentional.

## Rules During Refactor

- Change one slice at a time and validate immediately.
- Prefer file moves and function extraction before logic rewrites.
- Keep existing naming behavior and result codes unchanged.
- Do not refactor adjacent unrelated code.
- If a behavior is not directly tested, add a focused test before changing it.
- Test helpers and fakes must not create unrelated intermediate files in the repository root; keep temporary and progress artifacts under temp directories and preserve regression checks for stray files such as `-progress`.

## Validation Commands

Use the narrowest command possible after each phase.

- Build tests: `xmake build tests`
- Run tests: `xmake run tests`

If a phase changes only a pure helper area, prefer running the relevant test subset if the test runner supports it.

## Suggested File Split

This is a target structure, not a mandatory first-pass move.

- `src/video/video_output_planning.cpp`
- `src/video/video_output_planning.h`
- `src/video/video_progress_parser.cpp`
- `src/video/video_progress_parser.h`
- `src/video/video_encode_runner.cpp`
- `src/video/video_encode_runner.h`
- `src/video/video_batch_execution.cpp`
- `src/video/video_batch_execution.h`

## Done Definition

- `src/video/video_process.cpp` becomes a thin orchestration layer.
- Every extracted module has one clear responsibility.
- Existing behavior-facing tests still pass.
- New orchestration tests cover the previously unprotected flow.
- Test runs do not leave unrelated intermediate artifacts in the repository root.
- There is no obvious duplication left between video entry points or between video packing flow and app pipeline packing flow.
