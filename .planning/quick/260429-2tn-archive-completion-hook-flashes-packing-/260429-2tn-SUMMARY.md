# Quick Task 260429-2tn: Fix completion hook flashing during finalizing - Summary

**Status:** complete
**Date:** 2026-04-28

## Problem
Archive completion hook in `packGroups()` set postfix text unconditionally, overwriting the "Finalizing /" spinner when another archive was still finalizing. This caused a brief "Packing: archive X/Y" flash during `zip.close()`.

## Fix
`src/pack/pack_service.cpp`: Archive completion hook now skips `setPostfixText` and `onCompactStatusText` when `finalizingCount > 0`, matching the file callback guard.

## Test Results
All 215 test cases passed (909 assertions).
