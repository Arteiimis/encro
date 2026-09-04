## MODIFIED Requirements

### Requirement: Persistable flags render collapsed negation names

In the help option tables, a boolean flag that has a registered negation form SHALL render its name cells with the collapsed `--[no-]name` long form in the long-flag cell following the short-first convention (for example `-p, --[no-]pack`). The negation form SHALL NOT be listed as a separate option entry.

#### Scenario: Collapsed rendering in brief help

- **WHEN** the user runs `encro -h`
- **THEN** the File operation group shows the pack option rendered as `-p, --[no-]pack`

#### Scenario: No separate negation entries

- **WHEN** the user runs `encro -hh`
- **THEN** no `--no-yes`, `--no-pack`, `--no-keep`, `--no-compress`, `--no-recursive`, or `--no-folder-summary` entry appears as its own line in the option tables
