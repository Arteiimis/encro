## MODIFIED Requirements

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
