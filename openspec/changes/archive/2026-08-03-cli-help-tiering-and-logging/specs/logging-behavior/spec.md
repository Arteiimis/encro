## Purpose

Every run writes a rotating diagnostic log file by default, and `-v` turns on console echo of the logs, replacing the removed `-e` flag.

## ADDED Requirements

### Requirement: Log file written on every run

Every run SHALL write the rotating verbose log file (`%LOCALAPPDATA%/encro/logs/encro_YYYYMMDD_HHMMSS.log`, kept 10) regardless of flags, and SHALL print the hint `Log file: <path>` at startup. Runs that only print help (`-h`/`-hh`/`--help`) or version (`--version`) SHALL NOT create a log file and SHALL NOT print the hint.

#### Scenario: Default run writes a log file
- **WHEN** the user runs `encro -i <input>` without `-v` or `--log-json`
- **THEN** a timestamped log file is created in the encro logs directory
- **AND** the run prints `Log file: <path>` on the console

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
