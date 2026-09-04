# cli-help-layout Specification

## Purpose

Renders help text with git-style column discipline: description columns hug the widest rendered option name (plus a fixed gap) instead of a fixed padded column, and the main help surfaces registered subcommands in a git-style commands section with one-line descriptions.

## Requirements

### Requirement: Option names render short-first in two aligned sub-columns

The help option tables SHALL render each option's flag names in two sub-columns before the description column: a short-flag cell first, then a long-flag cell. The short-flag cell SHALL contain the option's short name formatted as `-x, ` and SHALL be padded to a constant width shared by every row of the same rendered help; when the option has no short name the cell SHALL be blank spaces of that same constant width. The long-flag cell SHALL contain the long name formatted as `--name`. The order SHALL be short flags first, long flags second (for example `-p, --pack`), for every rendered help: the brief tier, the full tier, and every subcommand help.

#### Scenario: Short flag leads its long name

- **WHEN** the user runs `encro -h`
- **THEN** the help option line for the yes flag starts with `-y, --[no-]yes`

#### Scenario: Option without a short name keeps long-name alignment

- **WHEN** the user runs `encro -h`
- **THEN** the version option line renders an empty short-flag cell and its `--version` long name starts at the same column as every other row's long name

#### Scenario: Subcommand help uses the same short-first layout

- **WHEN** the user runs `encro preview -h`, `encro config -h`, or `encro completion -h`
- **THEN** the subcommand's option lines use the same short-first two-cell layout as the main help

### Requirement: Description column auto-fits to the widest rendered option name

The help output SHALL place every option description in a column that starts after the widest long-flag cell text (long name plus any `(=default)` suffix) among the options actually rendered in that help, with a 3-space gap after that widest text and a 2-space indent before the short-flag cell. The width SHALL be computed globally across all option groups of the same rendered help, not per group, and SHALL be computed separately for each rendered help: the brief tier and the full tier each use only the options visible in that tier, and each subcommand help uses only its own options. Because the short-flag cell has a constant width, it SHALL NOT change which option determines the auto-fit width. When a `COLUMNS`-derived maximum description column applies, it SHALL still cap the auto-fitted width so no help line exceeds the configured line length; in that capped case the widest text SHALL keep a minimum 2-space gap.

#### Scenario: Brief tier aligns to its own widest option

- **WHEN** the user runs `encro -h`
- **THEN** the widest long-flag cell text among the brief-tier options is followed by exactly 3 spaces before its description
- **AND** every other option's description starts at that same column

#### Scenario: Full tier aligns to its own widest option

- **WHEN** the user runs `encro -hh`
- **THEN** the widest long-flag cell text among all rendered options (including advanced ones) is followed by exactly 3 spaces before its description
- **AND** every other option's description, in every group, starts at that same column

#### Scenario: Subcommand help aligns to its own options

- **WHEN** the user runs `encro preview -h` or `encro config -h`
- **THEN** the subcommand's descriptions align after the widest long-flag cell text of the subcommand's own rendered options, with the same 3-space gap rule

#### Scenario: Narrow terminal still caps the column

- **WHEN** the user runs `encro -h` with `COLUMNS=72`
- **THEN** the usage, commands, and option sections contain no line longer than 72 characters
- **AND** the auto-fitted description column is capped so wrapping remains

### Requirement: Main help lists subcommands in a commands section

The main help (`-h` and `-hh`) SHALL include an `encro commands:` section between the usage block and the option groups. The section SHALL list every registered subcommand, one per line, with the subcommand name followed by its one-line description aligned by the same auto-fit rule (2-space indent, widest command name, 3-space gap). The descriptions SHALL come from the registered subcommand descriptions.

#### Scenario: Commands section in brief help

- **WHEN** the user runs `encro -h`
- **THEN** the output contains an `encro commands:` section listing `preview` with the description `compare an original video with its encoded output side by side`, `config` with the description `inspect and persist user-level configuration defaults`, and `completion` with the description `print, install, or uninstall shell completion scripts`

#### Scenario: Commands section in full help

- **WHEN** the user runs `encro -hh`
- **THEN** the same `encro commands:` section is present

### Requirement: Main-help usage section omits subcommand synopses

The main-help usage section SHALL list only the flag-driven mode lines and the help/version line. It SHALL NOT contain a usage synopsis line for any registered subcommand, because the commands section carries them. It SHALL still document the positional input form and SHALL still include the `encro -h | -hh | --version` line required by the help-tiering capability.

#### Scenario: Subcommand synopsis lines removed from usage

- **WHEN** the user runs `encro -h` or `encro -hh`
- **THEN** the usage section contains no line starting with `encro preview` or `encro config`
- **AND** the usage section still contains the `encro -h | -hh | --version` line and a line documenting the positional input form

#### Scenario: Subcommand help keeps its own usage line

- **WHEN** the user runs `encro preview -h`
- **THEN** the preview help still shows the `encro preview <original> [<encoded>] [...]` usage line

### Requirement: Help layout is color-mode invariant

Help layout (line breaks and column positions) SHALL be computed from plain-text widths only; ANSI styling SHALL NOT influence wrapping. Rendering the same help with color enabled and with color disabled SHALL produce the same layout, including when a `COLUMNS` width constraint applies.

#### Scenario: Identical layout across color modes under a width constraint

- **WHEN** the user runs `encro -hh` with `COLUMNS=120` once with color forced on and once with color disabled
- **THEN** after stripping ANSI escape sequences the two outputs are line-identical
