# shell-completion Specification

## Purpose

Lets terminals complete encro's command surface: `encro completion <shell>` prints a ready-to-load completion script for `powershell` or `bash`, generated from the CLI's own registered definitions so completed names, values, and config keys can never drift from what the parser actually accepts.

## ADDED Requirements

### Requirement: Completion scripts are generated per shell on demand

`encro completion <shell>` SHALL print a completion script for the requested shell to stdout. The supported shells SHALL be `powershell` and `bash`. Requesting an unsupported shell SHALL fail with a non-zero exit and an error naming the supported shells. Running `encro completion` with no shell SHALL show the completion subcommand help.

#### Scenario: Bash script is printed

- **WHEN** the user runs `encro completion bash`
- **THEN** a bash completion script is printed to stdout and the exit code is 0

#### Scenario: PowerShell script is printed

- **WHEN** the user runs `encro completion powershell`
- **THEN** a PowerShell completion script is printed to stdout and the exit code is 0

#### Scenario: Unsupported shell is rejected

- **WHEN** the user runs `encro completion zsh`
- **THEN** the command exits non-zero and the error names `powershell` and `bash` as the supported shells

#### Scenario: Bare invocation shows help

- **WHEN** the user runs `encro completion` with no shell
- **THEN** the completion subcommand help is shown

### Requirement: Generated scripts complete the full registered command surface

A generated script SHALL offer as candidates every option registered for the current command context (short forms, long forms, and negated flag variants such as `--no-keep`), every registered subcommand (`preview`, `config`, and `completion`) at the main-command position, and `--help`/`--version`. Internal notation SHALL NOT appear in candidates (for example a flag's `{false}` suffix is stripped). Candidates SHALL match the current command context: options registered only on the main command SHALL NOT be offered inside a subcommand context, and subcommand-only options SHALL NOT be offered outside it. The script SHALL present candidates that extend the word being completed: the candidate set for the current context is filtered by the typed prefix before display.

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

#### Scenario: Candidates respect subcommand context

- **WHEN** the user types `encro preview --` and requests completion
- **THEN** the candidates include preview context options `--start` and `--no-open`
- **AND** main-command-only options such as `--pack` are not offered

### Requirement: Generated scripts complete enumerated values and config keys

For options whose legal values are enumerated by the CLI's own validation (for example `-f/--output-format`, `--color`, `--preset`, `-t/--type`, `--force-conflict-handling`), the script SHALL offer those legal values as value candidates. After `config --set` the script SHALL offer the configurable keys, and after a key whose values are enumerated the script SHALL offer that key's legal values. Numeric options (for example `--crf`, `-j/--jobs`) and free-text value options that take neither enumerated values nor paths (for example `--video-codec`) SHALL NOT offer value candidates.

#### Scenario: Enumerated option values are offered

- **WHEN** the user types `encro --output-format ` and requests completion
- **THEN** the candidates are `mp4` and `webp`

#### Scenario: Config keys are offered after --set

- **WHEN** the user types `encro config --set ` and requests completion
- **THEN** the candidates include the configurable keys such as `jobs`, `crf`, and `output-format`

#### Scenario: Config key values are offered after the key

- **WHEN** the user types `encro config --set output-format ` and requests completion
- **THEN** the candidates are `mp4` and `webp`

#### Scenario: Numeric options offer no values

- **WHEN** the user types `encro --crf ` and requests completion
- **THEN** no value candidates are offered

### Requirement: Generated scripts hide candidates that conflict with typed flags

The script SHALL NOT offer an option (under any of its names) when the typed words already contain an option that the CLI's own exclusion rules pair with it. Hiding SHALL be symmetric regardless of which side of a pair declared the exclusion, and SHALL apply to every name of the hidden option including short forms and negation variants. Path completion delegated to the shell is not subject to this filtering.

#### Scenario: Mutually exclusive pair is hidden after either side

- **WHEN** the user types `encro --resume --re` and requests completion
- **THEN** `--restart` is not offered
- **AND WHEN** the user types `encro --restart --re` and requests completion
- **THEN** `--resume` is not offered

#### Scenario: All names of a hidden option are hidden in both directions

- **WHEN** the user has typed `-i <path>` and requests completion for `--in`
- **THEN** `--inputs` is not offered
- **AND WHEN** the user has typed `--inputs <path>` and requests completion for `-`
- **THEN** `--input` and `-i` are not offered

#### Scenario: Config actions hide each other

- **WHEN** the user has typed `encro config --set jobs 4 --` and requests completion
- **THEN** `--list`, `--get`, `--unset`, and `--path` are not offered, but `--set`-compatible remaining options are still offered

### Requirement: Path options delegate to the shell's native file completion

For options and positionals that take file or directory paths (input, inputs, output, state file, ffmpeg path, positional inputs, preview's original/encoded/output), the script SHALL delegate to the shell's native file-name completion instead of offering candidates from the CLI definitions.

#### Scenario: Input path completes as a file

- **WHEN** the user types `encro -i ` and requests completion in a directory containing files
- **THEN** the candidates are the shell's native file completions, not option names

### Requirement: Emission is deterministic, side-effect free, and config-independent

For the same build and the same shell argument, repeated emission SHALL produce byte-identical output. Emission SHALL NOT install anything, SHALL NOT modify any user file, and SHALL produce the same script regardless of the user's stored config values or whether the config file exists or is corrupt.

#### Scenario: Repeated emission is byte-identical

- **WHEN** `encro completion bash` is run twice in the same build
- **THEN** the two outputs are byte-identical

#### Scenario: Stored config does not change the script

- **WHEN** a non-default value such as `output-format=webp` is stored in the user config and `encro completion powershell` is emitted
- **THEN** the output is identical to emission with no config file present
- **AND** emission succeeds even when the config file exists but is corrupt
