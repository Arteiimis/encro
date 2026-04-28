---
status: complete
one_liner: "Removed per-archive pack success messages after webp+pack progress bar"
---
# Quick Task 20260426-remove-pack-per-file-msg: Remove per-archive pack success messages

## Goal
After encoding webp and packing (video mode), the progress bar finishes and then each packed archive path is individually printed. Remove these per-package messages so only the final encoding summary is displayed, matching the behavior of pure-pack and picture modes.

## Steps
1. Remove the `for` loop in `packEncodedVideos()` (`src/video/video_process.cpp:428-432`) that prints "Packed archive: ..." for each zip.
2. Verify the final `printEncodingSummary()` still runs and produces the summary.
