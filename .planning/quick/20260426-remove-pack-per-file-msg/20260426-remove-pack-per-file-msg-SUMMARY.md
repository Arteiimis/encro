---
status: complete
---

# Quick Task 20260426-remove-pack-per-file-msg — Summary

## What was done
Removed the per-archive success messages (`"Packed archive: <path>"`) from `packEncodedVideos()` in `src/video/video_process.cpp`. After packing completes, the flow returns to `runScannedEncodingWorkflow()` which calls `printEncodingSummary()` — the final summary now shows "All encoding tasks completed." with encoding stats, matching the single-summary behavior of pure-pack and picture modes.

## Commit
`9ee3e96` — fix: remove per-archive success messages after webp+pack progress bar
