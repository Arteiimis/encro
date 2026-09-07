## MODIFIED Requirements

### Requirement: -v echoes logs to the console

The `-v`/`--verbose` flag SHALL add console echo of log records to stderr. With one occurrence (`-v`), the echo SHALL carry info- and warning-level records in a short format (lowercase level name, a colon, the message — no timestamp, module tag, source location, or attribute chain); error- and critical-level records SHALL NOT be echoed at this level because the clean `error:` console line owns them. With a second occurrence (`-vv`) or the alias `--debug`, the echo SHALL carry debug-level and above in the same format as the file log. Echo at any level SHALL disable progress bars, printing a notice when it does so. The rotating file log SHALL record the full debug set regardless of echo level.

#### Scenario: Echo enabled

- **WHEN** the user runs `encro -v -i <input>`
- **THEN** info-level log lines are echoed to stderr in the short format, in addition to being written to the file

#### Scenario: Echo disables progress bars

- **WHEN** the user runs `encro -v` in video mode with more than one video
- **THEN** progress bars are not shown
- **AND** a notice stating that progress bars are disabled is printed

#### Scenario: Echo goes to stderr, not stdout

- **WHEN** the user runs `encro -v -i <input> > out.txt`
- **THEN** `out.txt` contains the run's product output (scan and summary lines) and no echoed log records

#### Scenario: -v short format omits developer fields

- **WHEN** an info record is echoed at `-v`
- **THEN** the echoed line shows the level name and message only, with no timestamp, module tag, `[file:line]`, or `[attrs: ...]` content

#### Scenario: -vv or --debug echoes the full debug set

- **WHEN** the user runs `encro -vv -i <input>` (or `encro --debug -i <input>`)
- **THEN** debug-level records are echoed to stderr in the same format as the file log

### Requirement: Errors always reach the console

Command failures SHALL print the clean error message line on stderr in every verbosity mode — including with echo enabled — exactly once per failure. The echo stream MAY additionally carry the underlying log record at the `-vv`/`--debug` level; at `-v` it SHALL NOT echo error-level records, so the clean line is never duplicated.

#### Scenario: Parse error without -v

- **WHEN** the user runs an unknown option without `-v`
- **THEN** the error text appears on the console as a clean `error:` line

#### Scenario: Parse error with -v

- **WHEN** the user runs an unknown option with `-v`
- **THEN** the clean error line appears exactly once, in the same form as without `-v`

## ADDED Requirements

### Requirement: --quiet suppresses narration

The `--quiet` flag SHALL suppress narration lines and progress bars for the run. Errors and warnings SHALL still print, the run's final summary line SHALL still print, and failure-path output — the error line, the failed-file list, and the failure log hint — SHALL NOT be suppressed. The rotating log file SHALL still be written with the full debug set. `--quiet` SHALL NOT change exit codes. Quiet and echo are independent: `--quiet` with `-v` suppresses narration while the echo stream still emits.

#### Scenario: Quiet run stays silent on success

- **WHEN** the user runs a successful encode with `--quiet`
- **THEN** no scan, scheduling, or progress output prints; the final summary line prints

#### Scenario: Quiet run still fails loudly

- **WHEN** the user runs a failing encode with `--quiet`
- **THEN** the `error:` line, the failed-file list, and the log-file hint print

#### Scenario: Quiet does not disable file logging

- **WHEN** the user runs with `--quiet`
- **THEN** a timestamped log file with debug records is written as usual
