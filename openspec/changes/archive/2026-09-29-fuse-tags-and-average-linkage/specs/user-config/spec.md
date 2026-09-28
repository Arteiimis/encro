# Delta Spec: user-config

## MODIFIED Requirements

### Requirement: Configurable key set

The config SHALL accept exactly these keys, named after the CLI long options: `color`, `output-format`, `force-conflict-handling`, `jobs`, `ffmpeg-path`, `image-quality`, `crf`, `min-vmaf`, `preset`, `video-codec`, `yes`, `pack`, `keep`, `compress`, `recursive`, `folder-summary`, `model-dir`, `identity-tau`. `encro config set` SHALL reject unknown keys and SHALL reject values that violate the option's rule (legal members or numeric range), exiting non-zero with an error naming the key and its legal values. Unknown keys found in a hand-edited config file SHALL be ignored, with a warning naming the key. The `model-dir` key SHALL accept any non-empty path string (the directory need not exist at set time); its built-in default is `~/.encro/models`. The `identity-tau` key SHALL accept a number greater than 0 and at most 1 — the organize subcommand's identity similarity threshold — and its built-in default is the calibrated combined-score default.

#### Scenario: Unknown key rejected on set

- **WHEN** the user runs `encro config set nosuchkey 1`
- **THEN** the command exits non-zero with an error naming `nosuchkey`, and the config file is unchanged

#### Scenario: model-dir set and get round-trips

- **WHEN** the user runs `encro config set model-dir D:\models` followed by `encro config get model-dir`
- **THEN** `D:\models` is reported from config, even though the directory does not exist

#### Scenario: model-dir rejects an empty value

- **WHEN** the user runs `encro config set model-dir ""`
- **THEN** the command exits non-zero with an error naming `model-dir` and its rule

#### Scenario: Invalid value rejected on set

- **WHEN** the user runs `encro config set crf 99`
- **THEN** the command exits non-zero with an error naming `crf` and its legal range

#### Scenario: Unknown key in hand-edited file is ignored with warning

- **WHEN** the config file contains a key outside the configurable set and the user runs any command
- **THEN** the run prints a warning naming the unknown key and continues with the remaining keys applied

#### Scenario: organize resolves model-dir by precedence

- **WHEN** the config file contains `model-dir` and `encro organize` runs without `--model-dir`
- **THEN** the config value is used, and an explicit `--model-dir` overrides it

#### Scenario: identity-tau set and get round-trips

- **WHEN** the user runs `encro config set identity-tau 0.72` followed by `encro config get identity-tau`
- **THEN** `0.72` is reported from config, and a later `encro organize <dir>` without `--identity-tau` decides at 0.72

#### Scenario: identity-tau rejects a value outside its range

- **WHEN** the user runs `encro config set identity-tau 1.5`
- **THEN** the command exits non-zero with an error naming `identity-tau` and its legal range, and the config file is unchanged
