## ADDED Requirements

### Requirement: Help-flag output honors the effective color mode

Every help requested through a help flag — the main brief tier (`-h`), the full tier (`-hh`), and each subcommand help (`preview`, `organize`, `config`, `completion` via their `-h`) — SHALL be colored by the color mode in effect at the moment the help is printed, not the process-initial mode. The effective mode follows the established color precedence (explicit CLI `--color`, else the stored user config `color` value, else `auto`). With the effective mode `never`, printed help SHALL contain no ANSI styling; with the effective mode `always`, printed help SHALL contain ANSI styling even when stdout is not a TTY. Bare `encro config` and bare `encro completion` (help printed without a help flag) are outside this requirement: the config store is deliberately not read on those paths, so their help renders under the `auto` mode.

#### Scenario: Config color never disables help colors

- **WHEN** the stored user config sets `color` to `never` and the user runs `encro -h` in an interactive terminal
- **THEN** the help printed to stdout contains no ANSI escape sequences

#### Scenario: CLI color never overrides an interactive terminal

- **WHEN** the user runs `encro --color never -h` in an interactive terminal
- **THEN** the help printed to stdout contains no ANSI escape sequences

#### Scenario: CLI color always colors help through a pipe

- **WHEN** the user runs `encro --color always -h` with stdout redirected to a file or pipe
- **THEN** the help printed to stdout still contains ANSI styling

#### Scenario: Subcommand help honors the effective color mode

- **WHEN** color is disabled by any of the above (config `never`, CLI `never`, or `auto` suppression) and the user runs `encro preview -h`, `encro organize -h`, `encro config -h`, or `encro completion -h`
- **THEN** the printed help contains no ANSI escape sequences
