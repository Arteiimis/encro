## MODIFIED Requirements

### Requirement: Main help lists subcommands in a commands section

The main help (`-h` and `-hh`) SHALL include an `encro commands:` section between the usage block and the option groups. The section SHALL list every subcommand this build offers on the host, one per line, with the subcommand name followed by its one-line description aligned by the same auto-fit rule (2-space indent, widest command name, 3-space gap). The descriptions SHALL come from the registered subcommand descriptions. A subcommand that is registered but unavailable on the host SHALL be omitted from this section, while its own `-h` SHALL still render that subcommand's help.

#### Scenario: Commands section in brief help

- **WHEN** the user runs `encro -h`
- **THEN** the output contains an `encro commands:` section listing `preview` with the description `compare an original video with its encoded output side by side`, `config` with the description `inspect and persist user-level configuration defaults`, and `completion` with the description `print, install, or uninstall shell completion scripts`

#### Scenario: Commands section in full help

- **WHEN** the user runs `encro -hh`
- **THEN** the same `encro commands:` section is present

#### Scenario: An unavailable subcommand is omitted from the commands section

- **WHEN** the user runs `encro -h` or `encro -hh` on a build without an inference backend and with `ENCRO_FAKE_TAGGER` unset
- **THEN** the `encro commands:` section contains no `organize` row, and the section's description column still auto-fits the rows that remain
- **AND** `encro organize -h` on that build still prints the organize subcommand help
