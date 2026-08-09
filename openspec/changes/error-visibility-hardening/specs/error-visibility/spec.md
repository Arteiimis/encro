# error-visibility

## Purpose

Guarantees that every operational failure, cancellation, and crash leaves a truthful, attributable, durable record in the log file, so post-incident diagnosis never has to guess from silence or false success.

## ADDED Requirements

### Requirement: State persistence failures are reported

When the job-state store cannot persist its snapshot (disk full, read-only directory, locked state file, failed rename), the failure SHALL be surfaced to the caller and SHALL produce an error-level log record. The program SHALL NOT continue as if the state had been saved.

#### Scenario: State file write fails
- **WHEN** the state file cannot be opened or written during a `mark*` or `flush` operation
- **THEN** an error-level log record is written describing the operation and the failure reason
- **AND** the failure is propagated to the caller of the save operation

#### Scenario: State file rename fails
- **WHEN** the atomic rename of the temporary state snapshot fails after both fallback attempts
- **THEN** an error-level log record is written with the rename failure reason

#### Scenario: Initialize flush fails
- **WHEN** the initial flush during `initialize` fails
- **THEN** initialization fails with an error that reaches the top-level error path and is logged

### Requirement: Task exceptions are recorded

When a task executed on the task executor throws an exception, the exception message SHALL be written to the log; the failure recorded for the task SHALL include the exception message rather than a generic placeholder.

#### Scenario: Encoding task throws
- **WHEN** a video encoding task throws a C++ exception inside the worker
- **THEN** the log contains an error record with the exception message
- **AND** the task's recorded failure reason includes the exception message

### Requirement: Pack task failures are never reported as success

A pack run in which any individual packing task failed or threw SHALL be reported as failed. The run summary SHALL NOT claim success when a task result is a failure.

#### Scenario: Packing task throws
- **WHEN** a packing task throws an exception while another task in the same group succeeds
- **THEN** the run is reported as failed
- **AND** the log contains an error record with the exception message
- **AND** the output does not claim that all files were packed successfully

#### Scenario: Packing task fails
- **WHEN** a packing task returns a failure result
- **THEN** the run is reported as failed and the failure is logged

### Requirement: Scan failures are distinguishable and reported

The media scanner SHALL distinguish "input root is not a readable directory" from "no matching files found". An unreadable or missing input root SHALL produce an error-level log record and a non-zero exit; an empty scan result SHALL remain a normal outcome.

#### Scenario: Input root is unreadable
- **WHEN** the input path is not a directory, cannot be opened, or permission is denied
- **THEN** an error-level log record identifies the root and the failure reason
- **AND** the run exits with a non-zero exit code and an error message naming the root

#### Scenario: Scan iteration fails mid-walk
- **WHEN** iterating a subdirectory fails during a recursive scan
- **THEN** a warning-level log record identifies the subdirectory and the failure reason
- **AND** the failure does not silently truncate the result set without any record

#### Scenario: No matching files
- **WHEN** the input root is readable but contains no files with matching extensions
- **THEN** the run reports "no matching files" without an error log record (unchanged behavior)

### Requirement: Cancellation leaves a log trail

When the user requests cancellation (Ctrl-C / stop signal), the log SHALL record the cancellation event. When the force-exit watchdog terminates the process after the grace period, the log SHALL contain a direct final record explaining the forced exit.

#### Scenario: Stop signal received
- **WHEN** a stop signal is received
- **THEN** an info-level log record is written stating that cancellation was requested

#### Scenario: Force-exit watchdog fires
- **WHEN** the process fails to exit within the grace period after a stop request and the watchdog force-terminates it
- **THEN** a direct log record is written to the log file immediately before termination stating that the process was force-exited

### Requirement: Crash reports are durably written

The crash report (reason + stacktrace) SHALL be written directly to the current log file bypassing the async queue, so it survives process death. The direct write SHALL fall back to the async logger and then stderr without loss of the report on the primary path. The direct-write line format SHALL match the spdlog pattern's timestamp precision so lines sort correctly.

#### Scenario: Crash with healthy log file
- **WHEN** an unhandled exception, fatal signal, or terminate occurs and the log file is writable
- **THEN** the crash report appears in the log file even though the process exits without draining the async queue

#### Scenario: Direct write fails
- **WHEN** the direct file append fails (file handle unavailable)
- **THEN** the report is still delivered through the fallback tiers (async logger, then stderr) with no silent loss on the primary path

#### Scenario: Direct write timestamps
- **WHEN** a crash line is written directly next to regular log lines from the same second
- **THEN** the crash line carries millisecond and timezone-offset precision matching the regular lines

### Requirement: Progress parse degradation is logged

When ffmpeg progress parsing fails or falls back, the log SHALL record the degradation instead of degrading silently.

#### Scenario: Progress file unparseable
- **WHEN** the progress file cannot be parsed during encoding
- **THEN** a warning-level log record states that progress parsing failed

#### Scenario: Segment end fallback
- **WHEN** a segment end time cannot be parsed and the code falls back to the nominal segment duration
- **THEN** a warning-level log record states the fallback and the affected segment

### Requirement: Forensic snapshot reflects the in-flight command

The environment snapshot attached to error records SHALL describe the command actually in flight at the time of the error, including retry attempts.

#### Scenario: WebP retry tier fails
- **WHEN** a WebP encoding attempt at a reduced quality tier fails and logs an error
- **THEN** the attached snapshot shows the command of the failing tier, not a stale earlier command

### Requirement: Recent log lines survive abnormal termination

The logging system SHALL periodically flush buffered log data so that, on abnormal termination (hard kill, power loss), at most a bounded tail of recent non-error lines is lost; error-level lines remain synchronously flushed.

#### Scenario: Process hard-killed
- **WHEN** the process is terminated without draining the async queue
- **THEN** log lines produced more than the flush interval before termination are present in the log file
