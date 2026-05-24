---
phase: 260524-ruq
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/video/video_batch_execution.h
  - src/video/video_batch_execution.cpp
  - tests/video/video_batch_execution_tests.cpp
autonomous: true
requirements: []

must_haves:
  truths:
    - "After a slot's encoding task finishes successfully, the slot bar turns green with 'Done: <filename>'"
    - "After a slot's encoding task fails, the slot bar turns red with 'Failed: <filename>'"
    - "On success, the slot bar shows 100% progress (remaining time display stops)"
    - "On failure, the slot bar retains its progress but changes tone to red"
    - "The elapsed time continues to display after task completion (desirable)"
    - "If no barIndex is assigned (compact mode), barDone is a no-op (no crash)"
  artifacts:
    - path: "src/video/video_batch_execution.h"
      provides: "barDone() method declaration on EncodingExecutionContext"
      contains: "void barDone"
    - path: "tests/video/video_batch_execution_tests.cpp"
      provides: "Tests for barDone success/failure behavior"
      contains: "barDone"
  key_links:
    - from: "runEncodingTask() in video_batch_execution.cpp"
      to: "barDone() on EncodingExecutionContext"
      via: "method call replacing barIdle()"
      pattern: "executionCtx\\.barDone\\("
---

<objective>
Fix the HEVC encoding progress bar bug: after a slot's encoding task completes, the bar displays a completion state (green/red) instead of resetting to idle (white, 0%, "[idle-X]").

Purpose: When all video tasks in a progress bar slot finish, the bar currently calls `barIdle()` which unconditionally resets to idle mode with the timer still running. The fix adds a `barDone()` method that shows the final outcome state.

Output: A new `barDone(barIndex, success, fileLabel)` method on `EncodingExecutionContext` wired into `runEncodingTask()` at line 221, replacing the `barIdle()` call. Two test cases verifying success and failure display behavior.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/quick/260524-ruq-hevc-idle/260524-ruq-CONTEXT.md

<interfaces>
Key types and contracts the executor needs. Extracted from codebase.

From src/core/progress.h:
```cpp
enum class Tone { Default, Overall, Active, Idle, Packing, Finalizing, Success, Failure };

class ProgressContext {
public:
  void setPostfixText(std::size_t barIndex, std::string_view promptText);
  void setProgress(std::size_t barIndex, float progress);
  void setTone(std::size_t barIndex, Tone tone);
};
```

From src/video/video_batch_execution.h, existing barIdle() pattern (to follow):
```cpp
void barIdle(std::optional<std::size_t> barIndex, std::size_t slot) {
    if (!barIndex.has_value()) { return; }
    progress().setTone(barIndex.value(), progress::Tone::Idle);
    progress().setProgress(barIndex.value(), 0.0f);
    progress().setPostfixText(barIndex.value(), std::format("Encoding: [idle-{}]", slot + 1));
}
```

From src/video/video_batch_execution.cpp, the call site to fix (line 221):
```cpp
executionCtx.barIdle(barIndex, slot);   // <-- this resets to idle, wrong
```

The `result` variable (bool, encoding success) and `fileLabel` variable (std::string, truncated filename) are both in scope at the call site.
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add barDone() method to EncodingExecutionContext with tests</name>
  <files>src/video/video_batch_execution.h, tests/video/video_batch_execution_tests.cpp</files>
  <behavior>
    - Test 1 (success path): barDone with success=true sets tone to Success, progress to 100%, postfix to "Done: test.mp4"
    - Test 2 (failure path): barDone with success=false sets tone to Failure, no progress change, postfix to "Failed: test.mp4"
    - Test 3 (nullopt guard): barDone with std::nullopt barIndex is a no-op (no crash, no calls to progress())
  </behavior>
  <action>
This is a TDD task. Write the test FIRST, ensure it fails to compile (RED), then implement to pass (GREEN).

**Step 1 (RED — write tests first):**

Add test cases to `tests/video/video_batch_execution_tests.cpp`. Since `EncodingExecutionContext` requires `AppContext`, `EncodingProgressState`, `plannedOutputFiles`, and `actionIds`, construct a minimal test fixture. Use aggregate initialization for `AppContext` (`AppContext{}`), create a minimal `EncodingProgressState`, and empty maps for the other fields.

The test pattern: create an `EncodingExecutionContext` with a known bar, call `barDone()` with success=true and success=false, then verify via the `progress::ProgressContext` internal state. Since `ProgressContext` has no public getters, add a `barDone` behavioral verification by calling the same sequence of `progress().setTone()`, `progress().setProgress()`, `progress().setPostfixText()` manually and comparing with what `barDone()` would produce — or simply verify `barDone()` compiles, links, and runs without crash as the integration-level check, plus test the tone logic directly.

Preferred approach (pragmatic, consistent with existing test patterns in this file): Verify that `barDone()` exists, compiles, and correctly branches on success/failure. Create the test by constructing an `EncodingExecutionContext` and calling `barDone()` with both success values. Assert on the behavioral contract (no crash, correct conditional logic).

**Step 2 (GREEN — implement barDone):**

Add the `barDone()` method declaration and implementation inside `EncodingExecutionContext` struct in `src/video/video_batch_execution.h`, placed immediately after the existing `barIdle()` method (around line 216).

Signature: `void barDone(std::optional&lt;std::size_t&gt; barIndex, bool success, std::string_view fileLabel)`

Implementation logic:
1. Guard: `if (!barIndex.has_value()) { return; }` — same early-return pattern as `barIdle`
2. Set tone: `progress().setTone(barIndex.value(), success ? progress::Tone::Success : progress::Tone::Failure);`
3. Set progress: only on success: `if (success) { progress().setProgress(barIndex.value(), 100.0f); }`
   - On failure, do NOT reset progress to 0 — elapsed time display continues naturally
4. Set postfix: `progress().setPostfixText(barIndex.value(), std::format("{}: {}", success ? "Done" : "Failed", fileLabel));`

Follow existing code conventions: trailing return type (`auto ... -> void` or just `void`), east const, member access via `progress()`.

**Step 3 (REFACTOR — after tests pass):**

Review for clarity. No duplication expected in such a small method.
  </action>
  <verify>
    <automated>xmake build tests &amp;&amp; xmake run tests "[video-batch-execution]"</automated>
  </verify>
  <done>
barDone() method exists in EncodingExecutionContext, new tests pass (barDone success + failure + nullopt guard), existing video-batch-execution tests still pass.
  </done>
</task>

<task type="auto">
  <name>Task 2: Wire barDone() into runEncodingTask, replacing barIdle()</name>
  <files>src/video/video_batch_execution.cpp</files>
  <action>
In `src/video/video_batch_execution.cpp`, inside `runEncodingTask()` (anonymous namespace, around line 221), replace:

```
executionCtx.barIdle(barIndex, slot);
```

with:

```
executionCtx.barDone(barIndex, result, fileLabel);
```

The `result` variable (bool from `encodeVideo()` return value) and `fileLabel` variable (`makeSlotLabel(vidPath)` truncated filename string) are both in scope at this location.

No other changes needed — the next `barEncodingStart()` call (when a new task is assigned to this slot) will naturally override the completion state with active encoding display. The `clearActive(slot)`, `markFinished()`, and `updateOverall()` calls remain unchanged.

Run the full test suite to verify no regressions.
  </action>
  <verify>
    <automated>xmake build tests &amp;&amp; xmake run tests</automated>
  </verify>
  <done>
runEncodingTask() calls barDone() instead of barIdle(). Full test suite passes. Progress bar slot shows green "Done: &lt;filename&gt;" on success and red "Failed: &lt;filename&gt;" on failure instead of white "[idle-X]".
  </done>
</task>

</tasks>

<verification>
- Build: `xmake build encro` compiles clean (no unused-variable warnings from the change)
- Unit tests: `xmake build tests && xmake run tests "[video-batch-execution]"` — new barDone tests pass
- Full suite: `xmake build tests && xmake run tests` — all existing tests pass, no regressions
- Manual verification: run `xmake build encro` then encode a video with HEVC output format. Observe the slot bar turns green with "Done: filename.mp4" after encoding completes, not white "[idle-X]"
</verification>

<success_criteria>
- [ ] `barDone()` method exists on `EncodingExecutionContext` in video_batch_execution.h
- [ ] `barDone()` sets Success/Failure tone, 100% progress (success only), and "Done:"/"Failed:" postfix
- [ ] `barDone()` is a no-op when `barIndex` is `std::nullopt`
- [ ] `runEncodingTask()` line 221 calls `barDone(barIndex, result, fileLabel)` instead of `barIdle(barIndex, slot)`
- [ ] New tests pass in `[video-batch-execution]` tag group
- [ ] Full test suite passes with zero regressions
- [ ] Encoding a video to HEVC shows green "Done: <filename>" bar on success, not white idle bar
</success_criteria>

<output>
Create `.planning/quick/260524-ruq-hevc-idle/260524-ruq-SUMMARY.md` when done
</output>
