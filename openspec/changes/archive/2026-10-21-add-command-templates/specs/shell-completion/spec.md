# Spec Delta

## MODIFIED Requirements

### Requirement: Generated scripts complete the full registered command surface

A generated script SHALL offer as candidates every option registered for the current command context (short forms, long forms, and negated flag variants such as `--no-keep`), every subcommand this build offers on the host (`preview`, `batch`, `config`, `completion`, and `template`, plus `organize` where an inference backend exists) at the main-command position, and `--help`/`--version`. Internal notation SHALL NOT appear in candidates (for example a flag's `{false}` suffix is stripped). Candidates SHALL match the current command context: options registered only on the main command SHALL NOT be offered inside a subcommand context, and subcommand-only options SHALL NOT be offered outside it. The script SHALL present candidates that extend the word being completed: the candidate set for the current context is filtered by the typed prefix before display.

#### Scenario: All name forms are in the candidate set

- **WHEN** the user types `encro -` and requests completion
- **THEN** the candidates include `-r`, `--recursive`, and `--no-recursive`

#### Scenario: Candidates are filtered by the typed prefix

- **WHEN** the user types `encro --recu` and requests completion
- **THEN** `--recursive` is offered
- **AND** `-r` and `--no-recursive` are not offered

#### Scenario: Negation notation is stripped

- **WHEN** completion candidates are generated for flags declared with negation variants
- **THEN** no candidate contains `{` or `}` notation

#### Scenario: Subcommands are offered at the main position

- **WHEN** the user types `encro pre` and requests completion
- **THEN** the candidates include `preview`

#### Scenario: The completion subcommand itself is offered

- **WHEN** the user types `encro comp` and requests completion
- **THEN** `completion` is offered

#### Scenario: The template subcommand is offered

- **WHEN** the user types `encro temp` and requests completion
- **THEN** `template` is offered

#### Scenario: Candidates respect subcommand context

- **WHEN** the user types `encro preview --` and requests completion
- **THEN** the candidates include preview context options `--start` and `--no-open`
- **AND** main-command-only options such as `--pack` are not offered

#### Scenario: An unavailable subcommand has no scope

- **WHEN** a build without an inference backend and with `ENCRO_FAKE_TAGGER` unset emits `encro completion bash` or `encro completion powershell`
- **THEN** the emitted script offers no `organize` candidate at the main position and contains no organize scope

#### Scenario: The fake backend restores the organize scope

- **WHEN** the same build emits `encro completion bash` with `ENCRO_FAKE_TAGGER` set
- **THEN** `organize` is offered at the main position and its scope carries the options an available build offers
