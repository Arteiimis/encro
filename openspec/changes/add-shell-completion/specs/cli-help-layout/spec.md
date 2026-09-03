## MODIFIED Requirements

### Requirement: Main help lists subcommands in a commands section

The main help (`-h` and `-hh`) SHALL include an `encro commands:` section between the usage block and the option groups. The section SHALL list every registered subcommand, one per line, with the subcommand name followed by its one-line description aligned by the same auto-fit rule (2-space indent, widest command name, 3-space gap). The descriptions SHALL come from the registered subcommand descriptions.

#### Scenario: Commands section in brief help

- **WHEN** the user runs `encro -h`
- **THEN** the output contains an `encro commands:` section listing `preview` with the description `compare an original video with its encoded output side by side`, `config` with the description `inspect and persist user-level configuration defaults`, and `completion` with the description `print, install, or uninstall shell completion scripts`

#### Scenario: Commands section in full help

- **WHEN** the user runs `encro -hh`
- **THEN** the same `encro commands:` section is present
