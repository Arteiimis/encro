# cli-help-tiering Specification

## Purpose

Lets users get a concise overview of common commands with `-h` while keeping the complete option reference available via `-hh` (repeated `-h`).

## Requirements

### Requirement: Brief help shows common options only

The `-h`/`--help` output SHALL show usage lines and only the non-advanced options, grouped as today (General / Input-Output / Processing / File operation), and SHALL end with the hint line `Run 'encro -hh' to view all options.`

Advanced options hidden from the brief help: `-v`, `--log-json`, `-F`, `--color`, `-I`, `--state-file`, `--force-conflict-handling`, `-x`, `--preset`, `--video-codec`.

#### Scenario: Brief help invoked once
- **WHEN** the user runs `encro -h` with no other flags
- **THEN** the output shows the usage lines and only the non-advanced options
- **AND** the output ends with the hint `Run 'encro -hh' to view all options.`

#### Scenario: Brief help on parse error
- **WHEN** the user runs an unknown option so the error path renders help
- **THEN** the rendered help is the brief tier (no advanced options)

### Requirement: Full help shows all options

The `-hh` output SHALL show every option including the advanced ones, SHALL NOT end with the `Run 'encro -hh'` hint, and SHALL be shown whenever the help flag was given at least twice.

#### Scenario: Full help via repeated short flag
- **WHEN** the user runs `encro -hh`
- **THEN** the output includes the advanced options (`-v`, `--log-json`, `-F`, `--color`, `-I`, `--state-file`, `--force-conflict-handling`, `-x`, `--preset`, `--video-codec`)
- **AND** the output has no `Run 'encro -hh'` hint line

#### Scenario: Full help via repeated invocations
- **WHEN** the user runs `encro -h -h`
- **THEN** the output is the full tier, identical to `encro -hh`

### Requirement: Help flag and usage line advertise the tiers

The `-h` option description SHALL read `show help; use -hh to show all options`, and the usage section SHALL include an `encro -h | -hh | --version` line.

#### Scenario: Help flag description
- **WHEN** the user runs `encro -hh`
- **THEN** the `-h, --help` line in the output reads `show help; use -hh to show all options`

#### Scenario: Usage line
- **WHEN** the user runs `encro -h` or `encro -hh`
- **THEN** the usage section includes the line `encro -h | -hh | --version`

### Requirement: Usage lines advertise the config subcommand

The usage section of both help tiers SHALL include a line showing the `encro config` invocation form with its available actions (list, get, set, unset, path).

#### Scenario: Config usage line in brief help
- **WHEN** the user runs `encro -h`
- **THEN** the usage section includes a line presenting `encro config` with its actions

#### Scenario: Config usage line in full help
- **WHEN** the user runs `encro -hh`
- **THEN** the usage section includes the same `encro config` invocation line

### Requirement: Persistable flags render collapsed negation names

In the help option tables, a boolean flag that has a registered negation form SHALL render its name column with the collapsed `--[no-]name` form following the existing long-name-first convention (for example `--[no-]pack,-p`). The negation form SHALL NOT be listed as a separate option entry.

#### Scenario: Collapsed rendering in brief help
- **WHEN** the user runs `encro -h`
- **THEN** the File operation group shows the pack option rendered as `--[no-]pack,-p`

#### Scenario: No separate negation entries
- **WHEN** the user runs `encro -hh`
- **THEN** no `--no-yes`, `--no-pack`, `--no-keep`, `--no-compress`, `--no-recursive`, or `--no-folder-summary` entry appears as its own line in the option tables
