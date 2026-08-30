# user-config Specification

## Purpose

Lets users persist preference defaults (encoder quality, parallelism, toolchain path, packing and output behavior) in a user-level JSON config file instead of repeating them on every command line, managed through an `encro config` subcommand.

## Requirements

### Requirement: Config file resolution

The config file SHALL be resolved in this order: (1) the path in the `ENCRO_CONFIG` environment variable when set and non-empty; (2) on Windows, `%LOCALAPPDATA%\encro\config.json` when `LOCALAPPDATA` is set, otherwise `%APPDATA%\encro\config.json`; (3) otherwise `$XDG_CONFIG_HOME/encro/config.json` when `XDG_CONFIG_HOME` is set, otherwise `~/.config/encro/config.json`. A missing config file SHALL behave as an empty configuration: the run proceeds with built-in defaults and prints no diagnostic.

#### Scenario: Env override selects the config file

- **WHEN** `ENCRO_CONFIG` points at a JSON file containing `{"crf": 23}` and the user runs `encro config get crf`
- **THEN** the value `23` is reported from that file

#### Scenario: Default location used without env override

- **WHEN** `ENCRO_CONFIG` is unset and `encro config path` runs
- **THEN** the printed path is under the platform user config root (`encro\config.json` on Windows, `encro/config.json` under the XDG config root otherwise)

#### Scenario: Missing file behaves as no config

- **WHEN** the resolved config file does not exist and the user runs a normal encode with default flags
- **THEN** the run proceeds with built-in defaults and exits successfully, and no config-related warning is printed

### Requirement: Precedence of value sources

Every configurable option SHALL take its effective value from the first source that expresses one, in this order: an explicit command-line value, the config file value, the built-in default.

#### Scenario: Command line beats config

- **WHEN** the config file contains `{"crf": 23}` and the user runs an encode with `--crf 30`
- **THEN** the encode uses crf 30

#### Scenario: Config beats built-in default

- **WHEN** the config file contains `{"crf": 23}` and the user runs an encode without `--crf`
- **THEN** the encode uses crf 23

#### Scenario: Built-in default without config or CLI

- **WHEN** the config file is missing or does not contain a key, and the command line does not pass the option
- **THEN** the run uses the option's built-in default

### Requirement: Configurable key set

The config SHALL accept exactly these keys, named after the CLI long options: `color`, `output-format`, `force-conflict-handling`, `jobs`, `ffmpeg-path`, `image-quality`, `crf`, `min-vmaf`, `preset`, `video-codec`, `yes`, `pack`, `keep`, `compress`, `recursive`, `folder-summary`. `encro config set` SHALL reject unknown keys and SHALL reject values that violate the option's rule (legal members or numeric range), exiting non-zero with an error naming the key and its legal values. Unknown keys found in a hand-edited config file SHALL be ignored, with a warning naming the key.

#### Scenario: Unknown key rejected on set

- **WHEN** the user runs `encro config set dry-run true`
- **THEN** the command exits non-zero with an error naming `dry-run` as not configurable, and the file is unchanged

#### Scenario: Invalid value rejected on set

- **WHEN** the user runs `encro config set crf 99`
- **THEN** the command exits non-zero with an error naming `crf` and its legal range

#### Scenario: Unknown key in hand-edited file is ignored with warning

- **WHEN** the config file contains a key outside the configurable set and the user runs any command
- **THEN** the run prints a warning naming the unknown key and continues with the remaining keys applied

### Requirement: Malformed config file handling

A config file that exists but cannot be parsed as JSON SHALL cause the run or config action to fail with an error naming the file and the parse problem, exiting non-zero; the file SHALL NOT be overwritten or silently ignored. The `path` action SHALL report the resolved location without reading the file content.

#### Scenario: Encode run with malformed file

- **WHEN** the config file contains invalid JSON and the user runs `encro <input>`
- **THEN** the run fails with a non-zero exit code and an error naming the config file, before any processing starts

#### Scenario: Config action with malformed file

- **WHEN** the config file contains invalid JSON and the user runs `encro config list` (or `get`/`set`/`unset`)
- **THEN** the action fails with a non-zero exit code and an error naming the config file

#### Scenario: Path action ignores content

- **WHEN** the config file contains invalid JSON and the user runs `encro config path`
- **THEN** the resolved file location is printed with exit code 0

### Requirement: Config values validated like CLI values

When an option's effective value comes from the config file, that value SHALL be subject to the same validation rules (legal members, numeric ranges) as the corresponding CLI option, and a violating value SHALL fail the run with the same native-style parse error that names the option and its legal values. A config value that the command line overrides SHALL NOT be applied or validated for that run.

#### Scenario: Out-of-range value in file fails the run

- **WHEN** the config file contains `{"crf": 99}` and the user runs `encro <input>` without `--crf`
- **THEN** the run fails during argument parsing with an error naming `--crf` and its accepted range

#### Scenario: Invalid stored value unused when CLI overrides

- **WHEN** the config file contains `{"crf": 99}` and the user runs `encro <input> --crf 20`
- **THEN** the run succeeds using crf 20 and the stored value is not validated for that run

### Requirement: Config file format and rewrite behavior

The config file SHALL be pretty-formatted JSON: indented, one key per line, keys in a stable documented order, and booleans/numbers stored as native JSON types. `set` and `unset` SHALL preserve every other existing key and SHALL rewrite the whole file in the canonical pretty form, so a hand-compacted or reordered file is canonicalized on the next write.

#### Scenario: File is pretty-formatted after set

- **WHEN** the user runs `encro config set crf 23` on a missing or empty config store
- **THEN** the resulting file is multi-line indented JSON containing the key `crf` with the JSON number `23`

#### Scenario: Set preserves unrelated keys

- **WHEN** the config file contains `{"crf": 23, "jobs": 4}` and the user runs `encro config set preset p5`
- **THEN** the file afterwards contains all three keys

#### Scenario: Hand-compacted file is canonicalized

- **WHEN** the config file was hand-edited into compact single-line form and the user runs any `encro config set` action
- **THEN** the file is rewritten in the canonical pretty form while keeping all values

### Requirement: Config subcommand actions

`encro config` SHALL provide exactly these actions: `list` printing every known key with its current effective value and its source (`config` or `default`); `get <key>` printing the effective value of one key; `set <key> <value>` persisting a validated value; `unset <key>` removing a key so it falls back to the built-in default; and `path` printing the resolved config file location. Each action SHALL exit 0 on success. Failures (unknown key, invalid value, wrong argument count) SHALL exit non-zero with a native-style error message. Bare `encro config` with no action SHALL print the config subcommand help and exit 0. Passing an unknown action SHALL fail with a native-style error and non-zero exit code.

#### Scenario: List shows values and sources

- **WHEN** the config file contains `{"crf": 23}` and the user runs `encro config list`
- **THEN** the output shows `crf` with value `23` and source `config`, and `jobs` with its built-in default value and source `default`

#### Scenario: Get prints one key

- **WHEN** the config file contains `{"crf": 23}` and the user runs `encro config get crf`
- **THEN** the output is `23` and the exit code is 0

#### Scenario: Set then run uses persisted value

- **WHEN** the user runs `encro config set jobs 4` and then an encode without `-j`
- **THEN** the encode runs with 4 parallel jobs

#### Scenario: Unset restores default

- **WHEN** the config file contains `{"jobs": 4}` and the user runs `encro config unset jobs` then `encro config get jobs`
- **THEN** the reported value is the built-in default and the key no longer appears in the file

#### Scenario: Get unknown key fails

- **WHEN** the user runs `encro config get dry-run`
- **THEN** the command exits non-zero with an error naming `dry-run` as not configurable

#### Scenario: Path prints location

- **WHEN** the user runs `encro config path`
- **THEN** the output is the single resolved config file path

#### Scenario: Bare config prints help

- **WHEN** the user runs `encro config` with no action
- **THEN** the config subcommand help is printed and the exit code is 0

#### Scenario: Unknown action fails

- **WHEN** the user runs `encro config export`
- **THEN** the run fails with a native-style error and a non-zero exit code

### Requirement: Config actions run standalone

Config actions SHALL NOT require an input path, SHALL NOT probe the ffmpeg toolchain, and SHALL NOT enter the encode, picture, pack-only, or preview workflows.

#### Scenario: List works without inputs or toolchain

- **WHEN** the user runs `encro config list` with no input arguments and no ffmpeg available
- **THEN** the listing succeeds and exits 0 without any encode attempt

### Requirement: Negation flags for persistable flags

Each persistable boolean flag (`yes`, `pack`, `keep`, `compress`, `recursive`, `folder-summary`) — all of which have built-in defaults of false — SHALL additionally accept a negation form `--no-<name>` that explicitly sets the option to false for that run. The negation form SHALL override a config-persisted true value, and the plain form SHALL keep setting true.

#### Scenario: Negation overrides persisted true

- **WHEN** the config file contains `{"pack": true}` and the user runs an encode with `--dry-run --no-pack`
- **THEN** the run does not pack outputs

#### Scenario: Plain form still sets true

- **WHEN** the config file contains `{"pack": false}` and the user runs an encode with `-p`
- **THEN** the run packs outputs

#### Scenario: Negation without config equals default behavior

- **WHEN** the config file does not contain a key and the user runs an encode with the negation form
- **THEN** the run behaves as with the option absent (built-in default)

### Requirement: Effective defaults shown in help default displays

For configurable options, the `(=...)` default display in `-h`/`-hh` SHALL show the effective default: the config value when the key is set, otherwise the built-in default.

#### Scenario: Help shows config-adjusted default

- **WHEN** the config file contains `{"crf": 23}` and the user runs `encro -h`
- **THEN** the `--crf` entry shows `(=23)`
