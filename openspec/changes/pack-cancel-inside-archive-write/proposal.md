## Why

A stop request is honoured only at task boundaries: `taskexec::runTasks` checks `stopsignal::isStopRequested()` before each task (`src/core/task_executor.cpp:111`), and Directory-mode packing builds a single group for the whole input tree, so that one check is the pack stage's only checkpoint. A stop that arrives while the archive's entries are being written is noticed only after the archive completes - the run reports `All files packed successfully`, exit 0, with no cancellation notice - or, when the write outlasts the ~3 s force-exit grace period, the watchdog ends the process (exit 130, `force exit: ...` in the log) with no `Packing canceled by user.` notice. The existing stop tests request the stop before any task runs, so this gap is invisible to the suite. `cancellation-reporting` already promises the run ends at the stage's next checkpoint and that packing announces its cancellation, so the archive write needs a checkpoint of its own.

## What Changes

- **A checkpoint inside the archive write.** Both of `Packer::packFilesToZip`'s entry loops (the entries+progress loop at `src/pack/packer.cpp:400-425`, used with `--full-progress`, and the entries+callbacks loop at `:476-495`, used by compact mode) check `stopsignal::isStopRequested()` at the head of every entry.
- **A pending stop aborts the archive, not reports it.** On a pending stop the loop discards the open archive (`libzippp::ZipArchive::discard`) and returns `Packing canceled by user.`, so no half-written archive is left behind for the run to report as packed. The existing funnel translation is unchanged: the pack funnel maps an aborted batch with a pending stop to the cancellation exit code and prints its single notice (`src/pack/pack.cpp:486-490`), and the resumable path marks the archive task interrupted (`pack.cpp:409-413`).
- **No behaviour change when no stop is pending:** the same entries are written, the same archives are produced, and the progress output is unchanged.
- **Tests:** a deterministic case in `tests/packer_tests.cpp` requests a stop from the mid-write entry callback and asserts the call aborts with no archive left; a second case asserts the full-progress loop also leaves no archive when a stop is already pending. No other test file is edited.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `cancellation-reporting`: "A stop request aborts the running stage" gains the mid-archive checkpoint: a stop that arrives while a pack step writes an archive's entries ends that archive at the next entry and leaves no half-written archive, and packing still prints one notice and exits 130.

## Impact

- `src/pack/packer.cpp`: the two entry loops gain the stop check and the discard (plus the `infra/stop_signal.h` include).
- `tests/packer_tests.cpp`: two new cases; no existing case edited.
- `openspec/specs/cancellation-reporting/spec.md` via the change's delta spec.
- No CLI, archive-format, job-state or resume surface changes; no new dependency; the force-exit watchdog stays a backstop for a run that reaches no checkpoint at all.
