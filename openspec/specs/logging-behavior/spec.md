# logging-behavior Specification

## Purpose

Every run writes a rotating diagnostic log file by default, and `-v` turns on console echo of the logs, replacing the removed `-e` flag.

## Requirements

### Requirement: Log file written on every run

Every run SHALL write the rotating verbose log file (`%LOCALAPPDATA%/encro/logs/encro_YYYYMMDD_HHMMSS.log`, kept 10) regardless of flags. Failed runs SHALL print the hint `Log file: <path>` naming the run's log file on stderr so the failure can be diagnosed. Successful runs and runs that only print help (`-h`/`-hh`/`--help`) or version (`--version`) SHALL NOT print the hint. Runs that only print help or version SHALL NOT create a log file.

#### Scenario: Default run writes a log file
- **WHEN** the user runs `encro -i <input>` without `-v` or `--log-json`
- **THEN** a timestamped log file is created in the encro logs directory
- **AND** the output does not contain a `Log file:` hint

#### Scenario: Failed run prints the log hint
- **WHEN** a run fails with an error (for example invalid arguments)
- **THEN** the console shows the error
- **AND** the output contains a `Log file: <path>` hint naming an existing log file that records the failure

#### Scenario: Successful run prints no hint
- **WHEN** the user runs `encro preview <original> <encoded>` successfully
- **THEN** the output does not contain a `Log file:` hint

#### Scenario: Failed subcommand run prints the log hint
- **WHEN** a subcommand fails inside its own body (for example `encro config --set jobs 4.5` with an invalid value)
- **THEN** the output contains a `Log file: <path>` hint naming an existing log file that records the failure

#### Scenario: Interrupted run prints no hint
- **WHEN** a run is cancelled via Ctrl-C
- **THEN** the output does not contain a `Log file:` hint

#### Scenario: Help run creates no log file
- **WHEN** the user runs `encro -h`, `encro -hh`, or `encro --help`
- **THEN** no log file is created
- **AND** the output does not contain a `Log file:` hint

#### Scenario: Version run creates no log file
- **WHEN** the user runs `encro --version`
- **THEN** no log file is created
- **AND** the output does not contain a `Log file:` hint

#### Scenario: Log directory creation fails
- **WHEN** the log directory cannot be created
- **THEN** the run continues with a warning, without a log file, without the `Log file:` hint

#### Scenario: JSON logging also keeps the plain log
- **WHEN** the user runs `encro --log-json -i <input>`
- **THEN** both a `.log` and a `.ndjson` file are written

### Requirement: -v echoes logs to the console

The `-v`/`--verbose` flag SHALL add console echo of log lines (same format as the file) and SHALL disable progress bars, printing a warning when it does so.

#### Scenario: Echo enabled
- **WHEN** the user runs `encro -v -i <input>`
- **THEN** log lines are printed to the console as well as written to the file

#### Scenario: Echo disables progress bars
- **WHEN** the user runs `encro -v` in video mode with more than one video
- **THEN** progress bars are not shown
- **AND** a warning stating that progress bars are disabled is printed

### Requirement: Errors always reach the console

Command failures SHALL be printed to the console when console echo is off, and SHALL be logged (never printed twice) when `-v` echo is on.

#### Scenario: Parse error without -v
- **WHEN** the user runs an unknown option without `-v`
- **THEN** the error text appears on the console

#### Scenario: Parse error with -v
- **WHEN** the user runs an unknown option with `-v`
- **THEN** the error text appears exactly once via the echo

### Requirement: --verbose-echo flag removed

The `-e`/`--verbose-echo` option SHALL be removed; passing it SHALL produce a parse error.

#### Scenario: Using the removed flag fails
- **WHEN** the user runs `encro -e <input>`
- **THEN** the run fails with a parse error naming the unknown option

#### Scenario: Removed flag absent from help
- **WHEN** the user runs `encro -hh`
- **THEN** no `--verbose-echo` option is listed

### Requirement: NDJSON records follow a documented schema

When `--log-json` is active, every line of the `.ndjson` file SHALL be a single JSON object conforming to the documented schema below. Existing consumers SHALL be able to ignore unknown fields; no field SHALL be renamed or removed once introduced.

| Field | Type | Semantics |
|---|---|---|
| `timestamp` | string | RFC 3339 UTC with millisecond fractional seconds (`YYYY-MM-DDTHH:MM:SS.sssZ`) |
| `level` | string | One of `trace`, `debug`, `info`, `warning`, `error`, `critical` |
| `module` | string | Dot-separated module tag (e.g. `video.encode`) |
| `source` | string | Code origin as `filename:line` |
| `message` | string | Human-readable message with no context/attribute suffix |
| `error_context` | array of string | Context frames for error/critical records; absent otherwise |
| `elapsed_ms` | integer | Optional; stage duration for `ScopedTimer` completion records |
| `run_id` | string | Stable identifier of the run; same value across all records of one run |
| `task_id` | string | Identifier of the task being processed; present only on records emitted inside a task execution |
| `input` | string | Path of the file being processed (for pack tasks, the output zip path); present only on records emitted while processing a specific file |
| `summary` | object | Present only on the final record of a run (see end-of-run summary requirement) |

Level values SHALL map to syslog severities as follows: `trace`→7, `debug`→6, `info`→6, `warning`→4, `error`→3, `critical`→2.

#### Scenario: Record fields per level
- **WHEN** any record is written to the `.ndjson` file
- **THEN** the record contains `timestamp`, `level`, `module`, `source`, and `message` with the specified types
- **AND** `error_context` is present when and only when the record's level is `error` or `critical` and context frames were active

#### Scenario: Level values are syslog-aligned
- **WHEN** a record is written
- **THEN** its `level` value is one of the six documented values, mapping to the documented syslog severity

### Requirement: NDJSON records carry run and task correlation

Records of a single program invocation SHALL carry a `run_id` value; once job state is active, `run_id` SHALL equal the job-state `jobId` and SHALL remain stable for the rest of the run. Records emitted before job state initializes SHALL carry the bootstrap run id established at logging setup. Records emitted while the task executor is running a task that declares an input SHALL carry that task's `task_id`, matching the job-state `TaskRecord.id` for video and pack tasks (both derive from the same path via the normalized path form); picture-compress records SHALL carry the per-file task id (`compress:<outputPath>`), which has no job-state counterpart because picture compression is tracked as a single phase-level task. Probe/prewarm helper tasks (video info caching) declare no input and SHALL carry neither `task_id` nor `input`. Correlation fields SHALL be captured at the call site and survive asynchronous logging; a record SHALL NOT be attributed to a task that finished before the record was emitted.

#### Scenario: Run id aligns with job state
- **WHEN** the program runs with job state active
- **THEN** every record emitted after job state initialization has `run_id` equal to the run's job-state `jobId`

#### Scenario: Records inside a task carry the task id
- **WHEN** a video or pack task executes on the task executor
- **THEN** records emitted during the task's execution carry `task_id` equal to the task's job-state `TaskRecord.id`

#### Scenario: Picture records carry a per-file task id
- **WHEN** a picture-compress task executes
- **THEN** records emitted during the task's execution carry `task_id` of the form `compress:<outputPath>`
- **AND** this id has no job-state `TaskRecord` counterpart (picture compression is tracked as one phase-level task)

#### Scenario: Records outside a task carry no task id
- **WHEN** a record is emitted before the first task starts or after the last task ends
- **THEN** the record has no `task_id` field

#### Scenario: File processing records carry the input path
- **WHEN** a record is emitted while a specific input file is being processed (encode, compress, or pack step)
- **THEN** the record carries the input path as `input`

### Requirement: JSON timestamps include milliseconds

The NDJSON `timestamp` SHALL include millisecond fractional seconds, matching the precision of the human-readable `.log` format. Records written within the same second SHALL remain distinguishable by their fractional part.

#### Scenario: Timestamp precision
- **WHEN** a record is written to the `.ndjson` file
- **THEN** its `timestamp` ends with `.sssZ` (three fractional digits and a `Z`)

### Requirement: End-of-run summary record

Every run that writes a log file SHALL end with a single summary record: in the `.ndjson` file when JSON logging is active, and in the `.log` file always. In the `.ndjson` file the summary SHALL be the last record and SHALL carry a `summary` object with: `status` (`success`, `failed`, or `interrupted`), `jobId` (when job state was active), `tasks_total` and `tasks_failed` (when job state was active), `elapsed_ms`, `log` (the log file path), and `level_counts` (count of records per level). In the `.log` file the summary SHALL be the last line, human-readable, carrying the same information. No record SHALL be written after the summary.

#### Scenario: Successful run summary
- **WHEN** a run completes successfully with `--log-json` active
- **THEN** the `.ndjson` file's last record contains a `summary` object with `status` equal to `success`
- **AND** the `.log` file's last line describes the successful run with the same counts

#### Scenario: Failed run summary
- **WHEN** a run fails (pipeline error, task failure, or crash path that still reaches shutdown)
- **THEN** the summary record reports `status` `failed` and `tasks_failed` reflects failed tasks

#### Scenario: Interrupted run summary
- **WHEN** a run is cancelled via Ctrl-C
- **THEN** the summary record reports `status` `interrupted`

#### Scenario: Summary is last
- **WHEN** a run ends
- **THEN** no log record follows the summary record in either format

### Requirement: Crash records are correlated and reach both formats

When the process terminates through an unhandled exception, `std::terminate`, a fatal signal, or the force-exit watchdog, the crash report SHALL be appended directly to the `.log` file (bypassing the async queue) in the existing `[timestamp] [critical] [infra.crash]` line format, and the line SHALL carry the current run's `run_id` (appended as `run_id=<id>`). When JSON logging is active, the same crash report SHALL additionally be written to the `.ndjson` file as a single-line NDJSON record with `level` `critical`, `module` `infra.crash`, `message` equal to the full crash text (including the stacktrace, with line breaks escaped), and `run_id` equal to the run id of surrounding records. The direct write SHALL never take a lock (the thread may have crashed while holding one); a lock-free snapshot of the run id serves crash records. Crash records MAY appear before or after the summary record depending on where the process died; they are a distinct channel from the end-of-run summary.

#### Scenario: Crash record in both formats
- **WHEN** a process crashes mid-run with `--log-json` active
- **THEN** the crash report appears as a `.log` line carrying `run_id=<id>`
- **AND** as a parseable single-line NDJSON record in the `.ndjson` file with `level` `critical`, `module` `infra.crash`, and the same `run_id`

#### Scenario: Crash without JSON logging
- **WHEN** a process crashes mid-run without `--log-json`
- **THEN** only the `.log` line is written
- **AND** no `.ndjson` file is created or touched
