# logging-behavior Delta

## ADDED Requirements

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
