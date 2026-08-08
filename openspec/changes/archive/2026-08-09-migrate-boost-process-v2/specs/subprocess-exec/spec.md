# subprocess-exec

## Purpose

Defines the observable contract of the subprocess execution utility (`exec2`/`ExecResult` in `src/utils`) that all ffmpeg/ffprobe invocations route through: exit status, captured output, line callbacks, and stop-signal cancellation semantics. Pins existing behavior as the baseline for the boost.process v1→v2 migration.

## ADDED Requirements

### Requirement: Command execution reports exit status

Running a command line MUST block until the child exits (or is canceled) and MUST report the child's exit code and process id. A failed launch (executable missing) MUST surface as an error rather than a silent zero exit code.

#### Scenario: Successful command

- **WHEN** a command exits with code 0
- **THEN** the result reports exit code 0 and the child's process id

#### Scenario: Failed command

- **WHEN** a command exits with a non-zero exit code
- **THEN** the result reports that exact non-zero exit code

#### Scenario: Missing executable

- **WHEN** the command's executable cannot be launched
- **THEN** the caller receives an error instead of a successful empty result

### Requirement: Output capture and stream merging

The child's standard output MUST be captured in full into the result. With stderr merging enabled (the default), standard error MUST be interleaved into the same captured output as standard out; with merging disabled, standard error MUST be discarded (not forwarded to the caller's own stderr).

#### Scenario: Merged stderr

- **WHEN** a command writes to both stdout and stderr with merging enabled
- **THEN** the result contains both streams' content

#### Scenario: Separate stderr

- **WHEN** a command writes to stderr with merging disabled
- **THEN** the result contains only stdout content and the child's stderr is not visible to the caller

#### Scenario: Large output

- **WHEN** a command produces output larger than a single pipe buffer
- **THEN** the full output is captured without deadlock and without truncation

### Requirement: Line-oriented output callbacks

When a line callback is supplied, each completed line of output MUST be delivered to it, split on line feeds. Carriage-return characters preceding a line feed MUST be stripped. Partial trailing output without a final newline MAY be delivered only as part of the final captured result, not as a callback line.

#### Scenario: Callback per line

- **WHEN** a command emits three lines and a callback is supplied
- **THEN** the callback is invoked exactly three times, once per line, in order

#### Scenario: CRLF output

- **WHEN** a command emits lines ending in CRLF
- **THEN** each callback line has the trailing carriage return stripped

#### Scenario: No trailing newline

- **WHEN** a command's final output has no trailing newline
- **THEN** the final partial line still appears in the captured result

### Requirement: Stop-request cancellation

When a stop is requested while the child is running, the child MUST be terminated, the result MUST report the cancellation exit code (130), and output captured up to that point MUST be returned. The call MUST return promptly after termination rather than waiting for natural child exit.

#### Scenario: Stop during long-running command

- **WHEN** a stop is requested while the child is still running
- **THEN** the child is terminated and the result reports exit code 130 with the partial output captured so far

#### Scenario: Stop already pending before the run

- **WHEN** a stop is requested before the command is executed and the child is launched
- **THEN** the child is terminated promptly and the result reports exit code 130

#### Scenario: Stop after child exit

- **WHEN** a stop is requested after the child has already exited
- **THEN** the result reports the child's real exit code and no spurious cancellation is reported

### Requirement: Unresponsive child fallback

If the child does not exit within the termination grace period after a stop-request terminate, the call MUST return anyway: the child handle is released (the child may keep running detached), and the result reports the cancellation exit code with partial output.

#### Scenario: Child ignores termination

- **WHEN** a stopped child ignores termination and remains running past the grace period
- **THEN** the call returns with exit code 130, partial output, and without blocking on the child's eventual exit
