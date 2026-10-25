# shell-completion Specification

## Purpose

Lets terminals complete encro's command surface: `encro completion <shell>` prints a ready-to-load completion script for `powershell` or `bash`, generated from the CLI's own registered definitions so completed names, values, and config keys can never drift from what the parser actually accepts.

## Requirements

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

### Requirement: Generated scripts complete enumerated values and config keys

For options whose legal values are enumerated by the CLI's own validation (for example `-f/--output-format`, `--color`, `--preset`, `-t/--type`, `--force-conflict-handling`), the script SHALL offer those legal values as value candidates. After the config action verbs `get`, `set`, and `unset` the script SHALL offer the configurable keys, and after `config set <key>` the script SHALL offer that key's legal values when they are enumerated. Numeric options (for example `--crf`, `-j/--jobs`) and free-text value options that take neither enumerated values, paths, nor a completion provider (for example `--video-codec`) SHALL NOT offer value candidates.

#### Scenario: Enumerated option values are offered

- **WHEN** the user types `encro --output-format ` and requests completion
- **THEN** the candidates are `mp4` and `webp`

#### Scenario: Config keys are offered after the action verbs

- **WHEN** the user types `encro config set ` and requests completion
- **THEN** the candidates include the configurable keys such as `jobs`, `crf`, and `output-format`

#### Scenario: Config key values are offered after the key

- **WHEN** the user types `encro config set output-format ` and requests completion
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

### Requirement: Path options delegate to the shell's native file completion

For options that take file or directory paths and are declared as path-taking in the CLI's own option definitions, the script SHALL delegate to the shell's native file-name completion instead of offering candidates from the CLI definitions. The path-taking set SHALL be derived from the option declarations (input, inputs, output, state file, ffmpeg path, and organize's model dir today), so an option registered as path-taking is covered without editing the completion generator. Subcommand positionals that take paths (preview's original/encoded, organize's dir) SHALL continue to fall through to the shell's native file completion; main-command positional inputs keep the existing main-scope behavior (subcommand names at the main position). This requirement governs the options of subcommands this build offers, so the delegation for a path option of an unoffered subcommand (organize's model dir on a build without an inference backend) is unreachable rather than broken.

#### Scenario: Input path completes as a file

- **WHEN** the user types `encro -i ` and requests completion
- **THEN** the candidates are the shell's native file completions, not option names

#### Scenario: Organize model dir completes as a directory

- **WHEN** the user types `encro organize --model-dir ` and requests completion on a build whose completion script offers `organize`
- **THEN** the candidates are the shell's native file completions, not an empty set

### Requirement: Emission is deterministic, side-effect free, and config-independent

For the same build and the same shell argument, repeated emission SHALL produce byte-identical output. Emission SHALL NOT install anything, SHALL NOT modify any user file, and SHALL produce the same script regardless of the user's stored config values, whether the config file exists or is corrupt, and which templates are stored, because dynamically-backed candidates are resolved at completion time rather than embedded.

#### Scenario: Repeated emission is byte-identical

- **WHEN** `encro completion bash` is run twice in the same build
- **THEN** the two outputs are byte-identical

#### Scenario: Stored config does not change the script

- **WHEN** a non-default value such as `output-format=webp` is stored in the user config and `encro completion powershell` is emitted
- **THEN** the output is identical to emission with no config file present
- **AND** emission succeeds even when the config file exists but is corrupt

#### Scenario: The template store does not change the script

- **WHEN** `encro completion bash` is emitted first with no templates stored and then after `anime` and `webp` were saved
- **THEN** the two outputs are byte-identical and neither contains a template name

### Requirement: Enumerated positional values complete at subcommand positional slots

For subcommand positionals whose legal values are enumerated by the CLI's own validation (for example the completion subcommand's shell argument), the script SHALL offer those legal values as candidates when the word being completed is the subcommand's next unfilled positional slot. Slot detection SHALL count only words that are neither flags nor values of a preceding value-taking option, so the candidates SHALL be identical regardless of where flags appear (`encro completion <TAB>` and `encro completion --install <TAB>` offer the same set) and words consumed as option values SHALL not advance the slot. A typed prefix with no legal match SHALL offer nothing and SHALL suppress the shell's native file fallback, matching enumerated option behavior. A positional slot whose candidates are backed by a completion provider offers that provider's candidates under the same rules instead of falling through. Subcommand positional slots without enumerated values or a provider (path positionals) SHALL keep falling through to the shell's native file completion, and slots beyond a subcommand's positional count SHALL offer nothing.

#### Scenario: Shell argument completes at the positional slot

- **WHEN** the user types `encro completion ` and requests completion
- **THEN** the candidates are `bash` and `powershell`

#### Scenario: Positional candidates are filtered by prefix

- **WHEN** the user types `encro completion p` and requests completion
- **THEN** the only candidate is `powershell`

#### Scenario: Flag position does not change positional candidates

- **WHEN** the user types `encro completion --install ` and requests completion
- **THEN** the candidates are `bash` and `powershell`

#### Scenario: Filled positionals leave no candidates

- **WHEN** the user types `encro completion bash ` and requests completion
- **THEN** no candidates are offered and no file fallback appears

### Requirement: Completion help teaches the flag-first canonical form

The completion subcommand help SHALL present its usage synopsis with the action flags before the shell argument (`encro completion [--install | --uninstall] <powershell|bash>`), followed by a concrete example line showing a complete invocation (for example `encro completion powershell --install`). The synopsis order documents the canonical form; the parser continues to accept the shell argument and the flags in either order.

#### Scenario: Synopsis shows flags before the shell argument

- **WHEN** the user runs `encro completion --help`
- **THEN** the usage section shows `encro completion [--install | --uninstall] <powershell|bash>` as the synopsis
- **AND** a concrete example line naming a full invocation (shell and flag) is shown

#### Scenario: Both argument orders remain valid

- **WHEN** the user runs `encro completion powershell --install` or `encro completion --install powershell`
- **THEN** both invocations are accepted and perform the same install

### Requirement: Dynamically-backed candidate slots are resolved at completion time

The script SHALL resolve a candidate slot that the emitted model marks as dynamically backed by invoking the binary's candidate provider when completion is requested, so those candidates reflect live state; slots not marked that way keep their embedded candidate data and invoke nothing. Candidates SHALL be filtered by the typed prefix. When the provider yields no match, or cannot be invoked at all, the script SHALL offer no candidates and SHALL suppress the shell's native file fallback.

#### Scenario: Prefix filters live candidates

- **WHEN** the templates `anime` and `webp` are stored and the user types `encro -T a` and requests completion
- **THEN** the only candidate is `anime`

#### Scenario: Slots that are not dynamic never invoke the binary

- **WHEN** the `encro` on PATH is replaced by a stub that exits non-zero and the user types `encro --output-format ` and requests completion
- **THEN** the candidates are still `mp4` and `webp`

#### Scenario: An unusable provider suppresses the file fallback

- **WHEN** the provider cannot be invoked (the binary is missing from PATH, or it exits non-zero) and the user types `encro -T ` and requests completion
- **THEN** no candidates are offered and no file names appear

### Requirement: The template value slot completes stored template names

When the word being completed is the value of `-T`/`--template`, the script SHALL offer the stored template names — the legal template names present in the templates directory — filtered by the typed prefix, whether or not `-T` has already carried a value on that command line. A typed word that matches no stored template SHALL offer nothing rather than an error.

#### Scenario: Template names are offered

- **WHEN** the templates `anime` and `webp` are stored and the user types `encro -T ` and requests completion
- **THEN** the candidates are `anime` and `webp`

#### Scenario: Long form completes the same set

- **WHEN** the same templates are stored and the user types `encro --template w` and requests completion
- **THEN** the only candidate is `webp`

#### Scenario: An empty store offers nothing

- **WHEN** no template is stored and the user types `encro -T ` and requests completion
- **THEN** no candidates are offered and no file names appear

### Requirement: The template name positional completes stored template names

The `template` subcommand's `<name>` positional SHALL offer the stored template names filtered by the typed prefix instead of the shell's file completion. Candidates belong to the slot rather than the action, so the same set is offered whatever action was typed; rejecting an argument the action does not accept stays the parser's arity error.

#### Scenario: Show completes names instead of files

- **WHEN** the template `anime` is stored, the current directory holds `other.json`, and the user types `encro template show ` and requests completion
- **THEN** the only candidate is `anime`

#### Scenario: Delete completes with the typed prefix

- **WHEN** the templates `anime` and `webp` are stored and the user types `encro template delete a` and requests completion
- **THEN** the only candidate is `anime`

#### Scenario: Every name-taking action completes the same set

- **WHEN** the templates `anime` and `webp` are stored and the user types `encro template unset ` and requests completion
- **THEN** the candidates are `anime` and `webp`

#### Scenario: A new name stays typable

- **WHEN** the user types `encro template save fresh --crf 24` after completing at the name slot without picking a candidate
- **THEN** the command saves a template named `fresh`

### Requirement: The template key positional completes templateable keys

The `template` subcommand's `<key>` positional SHALL offer the templateable keys — the key set the generated template schema declares — as static candidates filtered by the typed prefix, whatever action was typed. Those candidates SHALL be embedded in the emitted script, so completing them invokes no candidate provider, and a key outside the templateable set SHALL never be offered.

#### Scenario: Keys are offered after the name

- **WHEN** the template `anime` exists and the user types `encro template unset anime ` and requests completion
- **THEN** the candidates include `crf`, `preset` and `output`

#### Scenario: Keys are filtered by prefix

- **WHEN** the user types `encro template unset anime cr` and requests completion
- **THEN** the only candidate is `crf`

#### Scenario: A non-templateable key is never offered

- **WHEN** the user types `encro template unset anime ` and requests completion
- **THEN** no candidate is `input` or `state-file`

#### Scenario: Key candidates need no provider call

- **WHEN** the `encro` on PATH is replaced by a stub that exits non-zero and the user types `encro template unset anime ` and requests completion
- **THEN** the candidates still include `crf` and `preset`

### Requirement: The candidate provider is a machine-readable channel

`encro completion --candidates <slot>` SHALL print the named slot's candidates one per line and exit 0, print nothing and exit 0 when that set is empty, and exit non-zero with an error naming an unknown slot id. It SHALL be mutually exclusive with `--install` and `--uninstall`. It SHALL resolve the templates directory as `encro template path` does, SHALL read no stored config values, and SHALL parse no template files, so a malformed template file leaves the candidate set intact.

#### Scenario: Names are printed one per line

- **WHEN** the templates `anime` and `webp` are stored and the user runs `encro completion --candidates template-name`
- **THEN** stdout is those two names, one per line in sorted order, and the exit code is 0

#### Scenario: An empty slot set is not an error

- **WHEN** no template is stored and the user runs `encro completion --candidates template-name`
- **THEN** stdout is empty and the exit code is 0

#### Scenario: An unknown slot is rejected

- **WHEN** the user runs `encro completion --candidates nosuch-slot`
- **THEN** the command exits non-zero and the error names `nosuch-slot`

#### Scenario: The channel cannot be combined with an install action

- **WHEN** the user runs `encro completion --candidates template-name --install`
- **THEN** the command is rejected during argument parsing, because the channel answers instead of acting on the installed script

#### Scenario: Candidates follow the slot, not the action

- **WHEN** the template `anime` is stored and the user types `encro template list ` and requests completion
- **THEN** the candidate is `anime`, and choosing it fails the action's arity check rather than being filtered out of the completion

#### Scenario: An illegal file stem is never offered

- **WHEN** the templates directory holds `anime.json` and a hand-placed `a b.json`
- **THEN** only `anime` is printed

#### Scenario: A malformed template file does not break the names channel

- **WHEN** the templates directory holds `anime.json` and a hand-edited `broken.json` that is not valid JSON, and the user runs `encro completion --candidates template-name`
- **THEN** `anime` is printed and the exit code is 0

#### Scenario: Stored config values are irrelevant

- **WHEN** the config file is corrupt and `ENCRO_CONFIG` points at it, and the user runs `encro completion --candidates template-name`
- **THEN** the command exits 0 and lists the stored template names
