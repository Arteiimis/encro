---
mode: quick
description: Change default user confirmation from N to Y
must_haves:
  truths:
    - readUserIpt function defaults to 'y' when input is empty
    - All prompt strings show "(Y/n)" instead of "(y/N)"
    - Existing yesToAll parameter behavior unchanged
  artifacts:
    - src/utils/utils.cpp
    - tests/utils_tests.cpp
    - src/picture/picture_process.cpp
    - src/video/video_process.cpp
    - src/video/video_batch_execution.cpp
  key_links:
    - src/utils/utils.cpp:303-314 (readUserIpt function)
    - tests/utils_tests.cpp:15-23 (existing test)
---

# Quick Task 260502-vwq: Change default user confirmation from N to Y

## Task 1: Update readUserIpt function and prompts

**Files:**
- src/utils/utils.cpp
- src/picture/picture_process.cpp
- src/video/video_process.cpp
- src/video/video_batch_execution.cpp
- tests/utils_tests.cpp

**Action:**
1. Modify `readUserIpt` function in `src/utils/utils.cpp`:
   - Change default response from 'n' to 'y' (line 308)
   - Keep all other logic unchanged

2. Update prompt strings to show "(Y/n)":
   - `src/picture/picture_process.cpp:525`: Change "(y/N)" to "(Y/n)"
   - `src/video/video_process.cpp:311`: Change "(y/N)" to "(Y/n)"
   - `src/video/video_batch_execution.cpp:308`: Change "(y/N)" to "(Y/n)"

3. Add test case in `tests/utils_tests.cpp`:
   - Add test for empty input returning true (default behavior)
   - Test should verify that pressing Enter (empty input) returns true

**Verify:**
- Run existing tests to ensure no regressions
- Verify new test passes
- Test the actual behavior by running the application

**Done:**
- All "(y/N)" prompts changed to "(Y/n)"
- Default behavior changed to Y (yes)
- Test coverage added for default behavior
- All existing tests pass
