---
name: exec2-pipe-close-race
description: exec2 with onLine callback intermittently delivers zero/partial lines (lines.size() == 0) only under coverage mode or load; process exits 0 but output callbacks never fire. Fixing by joining before closing the pipe reader.
---

# exec2 pipe close race: close the reader only after the reader thread finishes

## Symptom

- `exec2(cmd, onLine)` with a very short-lived child (`printf "alpha\r\nbeta\r\n"`) intermittently delivers 0 callbacks even though `exitCode == 0`.
- Fails in coverage-mode CI jobs (and under CPU load), passes in debug/release — the widened fork-to-exec window (see xmake-coverage-gcov-double-instrumentation) makes the race nearly deterministic in coverage mode.

## Root cause

In `src/utils/utils.cpp` the normal path was:

```cpp
process.wait();
closePipeReader();                        // ← closes the read end immediately
return {process.exit_code(), joinReader(), capturedPid};
```

The reader thread may not have consumed the kernel pipe buffer yet; closing the read end makes its blocked `read_some` return an error, dropping buffered output. The shorter the child's life and the less CPU the reader thread gets, the higher the probability.

## Fix

Join the reader first — the child has exited, so its write end is gone and the pipe hits EOF on its own:

```cpp
process.wait();
auto const output = joinReader();
closePipeReader();
return {process.exit_code(), output, capturedPid};
```

Safe because boost::process::v2's posix `process_io_binding(int fd)` (used when passing a native handle via `process_stdio{.out = writeEnd, ...}`) does NOT own/close the fd (`fd_needs_closing = false`), so the parent holds no write-end copy — EOF is guaranteed once the child exits. Do NOT swap the order back; closing first reintroduces the race.

## Verify

On Linux/WSL under load (`for i in $(seq 1 $(nproc)); do yes >/dev/null & done`), run the CRLF test 100+ times with a timeout guard: 0 failures and 0 hangs (the old code fails ~50%).
