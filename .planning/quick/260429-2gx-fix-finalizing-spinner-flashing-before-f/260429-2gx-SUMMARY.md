# Quick Task 260429-2gx: Fix finalizing spinner flashing - Summary

**Status:** complete
**Date:** 2026-04-28

## Changes

### Problem
1. Finalizing text flashed during file packing because spinner thread and file callbacks raced to update the same progress bar
2. Finalizing phase showed full packing status inline with spinner (`"Packing: archive X/Y [file A/B] | Finalizing /"`)

### Fix
In `src/pack/pack_service.cpp`:
1. Spinner now shows only `"Finalizing <frame>"` without packing status prefix
2. File callback skips `setPostfixText` and `onCompactStatusText` when `finalizingCount > 0` (doesn't overwrite spinner)
3. Archive completion hook restores normal packing status text after finalizing ends

### Test Results
All 215 test cases passed (909 assertions).
