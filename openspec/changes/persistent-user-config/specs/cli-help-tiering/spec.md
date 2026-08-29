# cli-help-tiering Specification

## ADDED Requirements

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
