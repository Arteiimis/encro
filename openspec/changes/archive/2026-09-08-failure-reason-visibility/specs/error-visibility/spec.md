## ADDED Requirements

### Requirement: Task failure reasons name the cause

When a subprocess-backed task fails (video encode, probe, picture compression), the task's recorded failure reason SHALL be the child's first meaningful diagnostic line — read from the separately captured stderr when the child ran with merging disabled, or from the retained merged-output capture when it ran with merging enabled — or an exit-code fallback (`exit code N`) when neither carries a diagnostic line. The reason SHALL be logged at warning level with the task's input, and every console failed-task list SHALL show it next to the failed file.

#### Scenario: Encode failure shows the ffmpeg message

- **WHEN** an encode task's ffmpeg exits non-zero after writing `Impossible to open 'out/.../seg_0.ts'` to its (merged) output
- **THEN** the log carries a warning record containing that line
- **AND** the post-run failed-file list prints the file path with that line as the reason

#### Scenario: Silent child falls back to the exit code

- **WHEN** a subprocess task fails with no diagnostic line in its captured output
- **THEN** the recorded reason and the failed-file list entry use the `exit code N` fallback instead of an empty string

#### Scenario: Long diagnostics collapse to one line

- **WHEN** a failing child writes a multi-line diagnostic
- **THEN** the surfaced reason is a single line (the first meaningful one), not the full diagnostic dump

## MODIFIED Requirements

### Requirement: Crash reports are durably written

The crash report (reason + stacktrace) SHALL be written directly to the current log file bypassing the async queue, so it survives process death. The direct write SHALL fall back to the async logger and then stderr without loss of the report on the primary path. In addition, whenever the log-file tier succeeds, the crash handler SHALL print a one-line crash reason plus the log-file path to the process's stderr, so the failure is visible on the terminal without opening the log; the full stacktrace goes only to the log file on that path. The direct-write line format SHALL match the spdlog pattern's timestamp precision so lines sort correctly.

#### Scenario: Crash with healthy log file

- **WHEN** an unhandled exception, fatal signal, or terminate occurs and the log file is writable
- **THEN** the crash report appears in the log file even though the process exits without draining the async queue

#### Scenario: Direct write fails

- **WHEN** the direct file append fails (file handle unavailable)
- **THEN** the report is still delivered through the fallback tiers (async logger, then stderr) with no silent loss on the primary path

#### Scenario: Direct write timestamps

- **WHEN** a crash line is written directly next to regular log lines from the same second
- **THEN** the crash line carries millisecond and timezone-offset precision matching the regular lines

#### Scenario: Crash reason reaches the terminal

- **WHEN** an unhandled exception terminates the run and the log file is writable
- **THEN** stderr contains a one-line crash reason naming the exception message and the log file path, without the full stacktrace
- **AND** the exit code is non-zero

#### Scenario: Broken log directory still reports

- **WHEN** the crash handler cannot write to the log file at all (both log tiers fail)
- **THEN** stderr carries the full crash report as the last-resort tier, unchanged from the fallback chain
