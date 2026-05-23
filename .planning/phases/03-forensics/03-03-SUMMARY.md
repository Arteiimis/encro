---
phase: 03-forensics
plan: 03
subsystem: logging, video, picture, pack
tags:
  - forensics
  - error-context
  - pipeline-wiring
  - environment-snapshot
provides:
  - ScopedErrorContext pipeline integration (13 placement sites)
  - Forensic context pointer wiring in video_batch_execution
  - FFmpeg subprocess PID/cmdline tracking
requires:
  - 03-01 (ScopedErrorContext + TLS context stack)
  - 03-02 (LOG_ERROR macro injection + captureEnvironmentSnapshot)
affects:
  - src/video/video_batch_execution.cpp
  - src/video/video_encode_runner.cpp
  - src/video/video_process.cpp
  - src/picture/picture_process.cpp
  - src/pack/pack_service.cpp
tech-stack:
  added: []
  patterns:
    - "RAII ForensicContextGuard for pointer cleanup on all exit paths"
    - "ScopedErrorContext mirrors ScopedTimer placement (same scope, adjacent declaration)"
    - "Per-iteration ScopedErrorContext in WebP adaptive retry loop"
    - "Mutable EncodingState& for PID writes in runStandardEncoding"
key-files:
  created: []
  modified:
    - src/video/video_batch_execution.cpp
    - src/video/video_encode_runner.cpp
    - src/video/video_process.cpp
    - src/picture/picture_process.cpp
    - src/pack/pack_service.cpp
key-decisions:
  - "ScopedErrorContext renamed to scopedCtx where ctx is already an AppContext parameter name"
  - "Forensic context pointers cleared via RAII guard (not manual cleanup at each return)"
  - "FFmpeg cmdline stored in encodeVideo() before dispatch; PID captured in runStandardEncoding() after exec2"
  - "WebpEncodeStep extended with optional pid for future forensic use"
metrics:
  duration: 11m
  completed-at: "2026-05-23T10:32:26Z"
  tasks: 3
  files_changed: 5
---

# Phase 3 Plan 3: Pipeline ScopedErrorContext Placement + Snapshot Summary

Wired the forensic infrastructure from Plans 03-01 and 03-02 into all pipeline stage boundaries, set forensic context pointers during encoding for environment snapshot access, and tracked FFmpeg subprocess PID and command line.

## One-Liner

ScopedErrorContext guards placed at all 13 pipeline stage boundaries (mirroring ScopedTimer sites) plus WebP retry loop, with forensic context pointer wiring and FFmpeg subprocess metadata tracking for automatic diagnostic chains.

## What Was Built

### Forensic Context Pointer Wiring (`src/video/video_batch_execution.cpp`)

- `logging::setForensicAppContext(&ctx)` and `logging::setForensicExecContext(&executionCtx)` called after EncodingExecutionContext construction
- `ForensicContextGuard` RAII struct clears both pointers on all exit paths (normal return, early return, exception)
- Enables `captureEnvironmentSnapshot()` to read active encoding slot state during LOG_ERROR

### FFmpeg Subprocess Metadata Tracking (`src/video/video_encode_runner.cpp`)

- `state.subprocessCmdline` populated in `encodeVideo()` before dispatching to encoding sub-function
- `state.subprocessPid` captured from `ExecResult.pid` in `runStandardEncoding()` after exec2 returns
- `runStandardEncoding()` signature changed from `EncodingState const&` to `EncodingState&` to support PID writes
- `WebpEncodeStep` extended with `std::optional<int> pid` for future forensic use

### ScopedErrorContext Pipeline Placement (4 files, 13 sites)

**video_process.cpp (4 sites):**
- `video.scan` — scanInputVideos() with input path detail
- `video.scan` — scanInputVideosFromFiles() with file count detail
- `video.encode` — runScannedEncodingWorkflow() with video count detail
- `video.pack` — packEncodedVideos() with input path detail

**picture_process.cpp (5 sites):**
- `picture.scan` — executeDirectPackWorkflow() with directory path detail
- `picture.pack` — executeDirectPackWorkflow() with picture count detail
- `picture.scan` — executeCompressPackWorkflow() with directory path detail
- `picture.compress` — executeCompressPackWorkflow() with task count and quality detail
- `picture.pack` — executeCompressPackWorkflow() with entry count detail

**pack_service.cpp (1 site):**
- `pack.execute` — packGroups() with group count detail

**video_encode_runner.cpp (3 sites):**
- `video.encode.webp` — encodeWebpWithTargetSize() entry with input path detail
- `webp.attempt` — per-iteration in WebP retry loop with quality and target info
- `video.encode` — encodeVideo() entry with input path detail

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed ScopedErrorContext name collision with AppContext& ctx parameter**
- **Found during:** Task 2 (compile step)
- **Issue:** `logging::ScopedErrorContext ctx(...)` collided with `appctx::AppContext& ctx` parameter in 9 of 13 placement sites. The C++ compiler resolved `ctx` to the ScopedErrorContext inside the lambda/scope, causing `ctx.config` and `ctx.toolchain` to fail (no such members on ScopedErrorContext).
- **Fix:** Renamed all ScopedErrorContext instances in shadowing scopes to `scopedCtx` (video_process.cpp: 4 sites, picture_process.cpp: 5 sites). Managed through careful unique-edit selection.
- **Files modified:** src/video/video_process.cpp, src/picture/picture_process.cpp

**2. [Rule 1 - Bug] Fixed corrupted encodeVideo() function from malformed edit**
- **Found during:** Task 2 (edit step)
- **Issue:** An edit intended to add ScopedErrorContext before dispatch accidentally duplicated the function body, leaving orphan code below the closing brace.
- **Fix:** Replaced the corrupted block by matching the duplicated code exactly, restoring the correct single body with ScopedErrorContext at entry.
- **Files modified:** src/video/video_encode_runner.cpp

## Test Results

```
Full suite: All tests passed (3359 assertions in 325 test cases)
[logging][error_context]: All tests passed (41 assertions in 16 test cases)
[logging][snapshot]: All tests passed (18 assertions in 5 test cases)
clang-format check passed for 122 files
```

## Commits

| Hash | Type | Message |
|------|------|---------|
| 52f5dac | feat | wire forensic context pointers and track FFmpeg subprocess metadata |
| 0d6d501 | feat | place ScopedErrorContext guards at all pipeline stage boundaries |
| adc110a | refactor | apply clang-format after ScopedErrorContext placement |

## Success Criteria Verification

- [x] ScopedErrorContext at all 10+ pipeline stage boundaries (4 video + 5 picture + 1 pack = 10)
- [x] ScopedErrorContext at retry loop entry + per-iteration in encodeWebpWithTargetSize
- [x] Forensic context pointers set before encoding, cleared after via RAII guard
- [x] FFmpeg cmdline stored in EncodingState before subprocess launch
- [x] FFmpeg PID captured from ExecResult after subprocess completes
- [x] Full test suite green with no regressions
- [x] clang-format check passes

## Self-Check: PASSED

- [x] src/video/video_batch_execution.cpp contains ForensicContextGuard + setForensicAppContext/setForensicExecContext
- [x] src/video/video_encode_runner.cpp contains subprocessCmdline write, subprocessPid capture, WebpEncodeStep.pid
- [x] src/video/video_process.cpp contains 4 ScopedErrorContext placements
- [x] src/picture/picture_process.cpp contains 5 ScopedErrorContext placements
- [x] src/pack/pack_service.cpp contains 1 ScopedErrorContext placement
- [x] Commit 52f5dac exists (Task 1: forensic context pointers + subprocess tracking)
- [x] Commit 0d6d501 exists (Task 2: ScopedErrorContext placement)
- [x] Commit adc110a exists (Task 3: clang-format)
- [x] All 325 test cases pass
- [x] clang-format check passes
- [x] East const, trailing return, PascalCase, camelCase conventions followed
- [x] string_view lifetime handled via local variables
