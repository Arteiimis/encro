# Quick Task 260429-1iq: 打包进度条显示错误修复 - Summary

**Status:** complete
**Date:** 2026-04-28

## Changes

### Root Cause
`src/pack/pack_service.cpp:packGroups()` compact mode callback used per-archive indices (`index+1/archiveCount`) and per-archive file counts (`fileIndex/fileCount`) for the single shared progress bar. With parallel execution of multiple archives, callbacks interleaved, causing the displayed values to jump between different archives' indices and file counts.

### Fix
1. Added `std::atomic<std::size_t> completedArchiveCount` to track cumulative completed archives
2. Modified initial status text to use `0/archiveCount` and `compactTotalFiles` instead of hardcoded `1/N` and `groups.front().size()`
3. Modified callback to use cumulative `completedArchiveCount/archiveCount` and `completedFileCount/compactTotalFiles`
4. Added archive completion update block: after each archive finishes (`packFilesToZip` returns), increment `completedArchiveCount` and update progress bar text

### Files Changed
- `src/pack/pack_service.cpp` — core fix in `packGroups()`
- `tests/pack_service_tests.cpp` — updated expected status text sequence for cumulative format

### Test Results
All 215 test cases passed (909 assertions).
