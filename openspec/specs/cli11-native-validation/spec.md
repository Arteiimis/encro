# cli11-native-validation Specification

## Purpose

Moves CLI argument validation, option conflicts/dependencies, and parse-time error reporting out of hand-written validation code and into the parsing layer's native mechanisms, with native-style error messages. Keeps the existing two-tier help output byte-stable.

## Requirements

### Requirement: Enumerated option values validated at parse time

Options with a fixed value set SHALL reject invalid values during argument parsing, before any processing starts, with an error message that names the option and its legal values.

Legal value sets:
- `-f/--output-format`: `mp4`, `webp`
- `--force-conflict-handling`: `y`, `n` (case-insensitive)
- `--color`: `auto`, `always`, `never` (case-insensitive)
- `--preset`: `auto`, `p1`..`p7`

#### Scenario: Invalid output format rejected
- **WHEN** the user runs `encro -i <dir> -f avi`
- **THEN** the run fails during argument parsing with a non-zero exit code and an error naming `-f`/`--output-format` and the legal values

#### Scenario: Invalid conflict-handling value rejected
- **WHEN** the user runs `encro -i <dir> --force-conflict-handling x`
- **THEN** the run fails during argument parsing, and `--force-conflict-handling Y` (upper case) is accepted

#### Scenario: Invalid color mode rejected
- **WHEN** the user runs `encro -i <dir> --color pink`
- **THEN** the run fails during argument parsing with an error naming the legal values

#### Scenario: Invalid preset rejected
- **WHEN** the user runs `encro -i <file> --preset p9`
- **THEN** the run fails during argument parsing, and `--preset auto` and `--preset p1`..`p7` are accepted

### Requirement: Process type aliases map to canonical values

The `-t/--type` option SHALL accept the canonical values `video` and `picture` and the aliases `vid` and `pic`, mapping aliases to their canonical value before configuration is built.

#### Scenario: Picture alias
- **WHEN** the user runs `encro -i <dir> -t pic`
- **THEN** the picture workflow runs, identical to `-t picture`

#### Scenario: Video alias
- **WHEN** the user runs `encro -i <dir> -t vid`
- **THEN** the video workflow runs, identical to `-t video`

#### Scenario: Unknown type rejected
- **WHEN** the user runs `encro -i <dir> -t film`
- **THEN** the run fails during argument parsing with an error naming the legal values

### Requirement: Numeric ranges validated at parse time

Options with numeric bounds SHALL reject out-of-range values during argument parsing with an error message naming the option and its accepted range:

- `-q/--image-quality`: 2..31
- `--crf`: 0..51
- `--min-vmaf`: 0..100
- `-j/--jobs`: >= 1
- preview `--start` and `--duration`: >= 0

#### Scenario: Image quality out of range
- **WHEN** the user runs `encro -i <dir> -c -q 99`
- **THEN** the run fails during argument parsing; `-q 2` and `-q 31` are accepted

#### Scenario: CRF out of range
- **WHEN** the user runs `encro -i <dir> --crf 99`
- **THEN** the run fails during argument parsing; `--crf 0` and `--crf 51` are accepted

#### Scenario: Jobs zero rejected
- **WHEN** the user runs `encro -i <dir> -j 0`
- **THEN** the run fails during argument parsing; `-j 1` is accepted

#### Scenario: Negative preview window rejected
- **WHEN** the user runs `encro preview <a> <b> --start -5`
- **THEN** the run fails during argument parsing

### Requirement: Option conflicts and dependencies rejected natively

The following option pairs SHALL be rejected when used together, with a native-style error message naming both options:

- `-i/--input` with `-I/--inputs`
- `--resume` with `--restart`
- `-p/--pack` with `-z/--pack-only`
- `--dry-run` with `--crf`

The `--image-quality` option SHALL be rejected when `--compress` is not given.

#### Scenario: Resume and restart conflict
- **WHEN** the user runs `encro -i <dir> --resume --restart`
- **THEN** the run fails with an error naming both `--resume` and `--restart`

#### Scenario: Dry-run and crf conflict
- **WHEN** the user runs `encro -i <file> --dry-run --crf 20`
- **THEN** the run fails with an error naming both options

#### Scenario: Image quality without compress
- **WHEN** the user runs `encro -i <dir> -q 10` without `-c/--compress`
- **THEN** the run fails with an error stating that `--image-quality` requires `--compress`

### Requirement: Parse errors use native-style messages

All argument-validation failures SHALL be reported through the parsing layer's native error mechanism: the message names the offending option and the legal values, conflict partner, or dependency, and the process exits with a non-zero exit code.

#### Scenario: Validation failure names option and legal values
- **WHEN** any validation failure from the requirements above occurs
- **THEN** the error text names the offending option and its legal values, conflict partner, or dependency

### Requirement: Preview subcommand help and validation come from option definitions

The `preview` subcommand SHALL validate its own arguments through the parsing layer: running `encro preview` without `original` SHALL fail with a native missing-argument error and a non-zero exit code, and `encro preview -h` SHALL print the subcommand's help with exit code 0. The help content SHALL be rendered from the subcommand's option definitions (names, types, descriptions) and SHALL keep the colored styling of the main help; it SHALL NOT be a separately maintained text blob.

#### Scenario: Missing original fails natively
- **WHEN** the user runs `encro preview` with no positional arguments
- **THEN** the run fails with a native missing-argument error naming `original` and a non-zero exit code

#### Scenario: Preview help from definitions
- **WHEN** the user runs `encro preview -h`
- **THEN** the help lists the subcommand's options with their current descriptions and exits with code 0

### Requirement: Main help output stays byte-stable

The two-tier help contract (`-h` brief tier, `-hh` full tier, option groups, advanced-option hiding, hint line, `(=default)` displays) SHALL remain stable across changes: help output SHALL change only in ways that an explicit capability specification declares (such as usage lines advertising subcommands, collapsed `--[no-]name` option-name rendering, or config-driven effective default displays), and SHALL otherwise preserve the same structure.

#### Scenario: Brief and full help unchanged
- **WHEN** the user runs `encro -h` and `encro -hh` and no capability specification declares a help-output change affecting the rendered content
- **THEN** the output preserves the same structure: same usage lines, same groups, same advanced-option hiding, same `Run 'encro -hh' to view all options.` hint, and same `(=value)` default displays