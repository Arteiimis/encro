# Spec Delta

## MODIFIED Requirements

### Requirement: Precedence of value sources

Every configurable option SHALL take its effective value from the first source that expresses one, in this order: an explicit command-line value, the value stored in the command template selected for the run, the config file value, the built-in default. An option the selected template does not store SHALL fall back to the config file value, and selecting a template SHALL NOT change the effective value of any option it does not store.

#### Scenario: Command line beats config

- **WHEN** the config file contains `{"crf": 23}` and the user runs an encode with `--crf 30`
- **THEN** the encode uses crf 30

#### Scenario: Config beats built-in default

- **WHEN** the config file contains `{"crf": 23}` and the user runs an encode without `--crf`
- **THEN** the encode uses crf 23

#### Scenario: Built-in default without config or CLI

- **WHEN** the config file is missing or does not contain a key, and the command line does not pass the option
- **THEN** the run uses the option's built-in default

#### Scenario: Template beats config

- **WHEN** the config file contains `{"crf": 23}`, the template `anime` stores `crf=24`, and the user runs `encro -T anime <dir>` without `--crf`
- **THEN** the run uses crf 24

#### Scenario: Option absent from the template falls back to config

- **WHEN** the config file contains `{"preset": "p5"}`, the template `anime` stores only `crf`, and the user runs `encro -T anime <dir>`
- **THEN** the run uses preset p5
