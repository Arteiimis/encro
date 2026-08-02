# cli-flag-cleanup Specification

## Purpose

Removes the dead `--flat` flag and makes every remaining option's help text accurate and self-explanatory.

## Requirements

### Requirement: Dead --flat flag removed

The `--flat` option SHALL be removed: passing it SHALL produce a parse error, and default output naming SHALL remain the flat layout.

#### Scenario: Using the removed flag fails
- **WHEN** the user runs `encro --flat <input>`
- **THEN** the run fails with a parse error naming the unknown option

#### Scenario: Default layout unchanged
- **WHEN** the user runs `encro -i <input>` without `--keep`
- **THEN** the run uses the flat output layout as before

### Requirement: Accurate --resume description

The `--resume` help text SHALL describe its strict-mode semantics: `require matching previous job state; error if missing or mismatched`.

#### Scenario: Resume description in help
- **WHEN** the user runs `encro -hh`
- **THEN** the `--resume` line reads `require matching previous job state; error if missing or mismatched`

### Requirement: Conflict handling help explains its values

The `--force-conflict-handling` help text SHALL explain the `y|n` values and the default (`y`).

#### Scenario: Conflict handling description in help
- **WHEN** the user runs `encro -hh`
- **THEN** the `--force-conflict-handling` line explains what `y` and `n` do and that `y` is the default

### Requirement: Default values shown uniformly

Options with default values SHALL display them in the `(=value)` form used by CLI11, matching `-f (=mp4)` and `-t (=video)`: `-q (=2)`, `--crf (=28)`, `--preset (=auto)`. No option description SHALL repeat the default in prose (`default=...` text) when the `(=value)` display is present, including `-j`, whose description drops `default=10`.

#### Scenario: Defaults in full help
- **WHEN** the user runs `encro -hh`
- **THEN** the lines for `-q`, `--crf`, and `--preset` show `(=2)`, `(=28)`, and `(=auto)` respectively
- **AND** no `default=` prose remains in the descriptions of `-j`, `-q`, `--crf`, or `--preset`

#### Scenario: Defaults in brief help
- **WHEN** the user runs `encro -h`
- **THEN** the `--crf` line shows `(=28)`

### Requirement: Encoder defaults consistent with help

The effective default CRF applied when `--crf` is not given SHALL be 28, and no `EncodeConfig` member default SHALL contradict it (dead member defaults are removed; the `buildCMD` `value_or` fallbacks are the single source of truth).

#### Scenario: NVENC command uses cq 28 by default
- **WHEN** an `EncodeConfig` is built without a CRF value
- **THEN** its ffmpeg command contains `-cq 28`

### Requirement: --keep help states the default layout

The `--keep` help text SHALL state that flattening is the default when `--keep` is not given.

#### Scenario: Keep description in help
- **WHEN** the user runs `encro -hh`
- **THEN** the `--keep` line mentions `(default: flatten)`
