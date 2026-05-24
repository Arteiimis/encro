# Quick Task 260524-ruq: 视频编码HEVC进度条完成后状态异常 - Context

**Gathered:** 2026-05-24
**Status:** Ready for planning

<domain>
## Task Boundary

视频编码为hevc流程中，当一个进度条槽位的所有视频任务结束后，进度条的状态和颜色不会变为完成，会回到idle模式并且计时器继续计时。

Root cause identified in `runEncodingTask()` (video_batch_execution.cpp:221):
after `finalizeState()` sets `finished=true, lastProgress=100`, the code calls
`barIdle()` which unconditionally resets the bar to white `Tone::Idle`, 0% progress,
"[idle-X]" text. The slot bar never shows a completion state.

</domain>

<decisions>
## Implementation Decisions

### Completion State Display
- After a task finishes, show completion state on the slot bar: Success (green) or Failure (red)
- Set progress to 100% for success (which also stops the "remaining time" display)
- Postfix text shows "Done: <filename>" / "Failed: <filename>"
- If more tasks are pending for this slot, the next `barEncodingStart()` will naturally override this completion state — no special handling needed

### Timer Behavior
- Setting progress to 100% causes `ShowRemainingTime` to display 0s, which is naturally correct
- `ShowElapsedTime` continues to show total elapsed — this is desirable (shows how long the task took)
- No need to dynamically toggle elapsed time options on the indicators library

### Claude's Discretion
- Implement a new `barDone()` method on `EncodingExecutionContext` (or inline logic) to replace `barIdle()` call at the end of `runEncodingTask()`
- The method sets tone to Success/Failure, progress to 100%, and appropriate postfix text
- All logic contained in `video_batch_execution.h` and `video_batch_execution.cpp`

</decisions>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches.

</specifics>

<canonical_refs>
## Canonical References

No external specs — requirements fully captured in decisions above.

</canonical_refs>
