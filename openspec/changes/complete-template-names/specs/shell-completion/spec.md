## ADDED Requirements

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

`encro completion --candidates <slot>` SHALL print the named slot's candidates one per line and exit 0, print nothing and exit 0 when that set is empty, and exit non-zero with an error naming an unknown slot id. It SHALL resolve the templates directory as `encro template path` does, SHALL read no stored config values, and SHALL parse no template files, so a malformed template file leaves the candidate set intact.

#### Scenario: Names are printed one per line

- **WHEN** the templates `anime` and `webp` are stored and the user runs `encro completion --candidates template-name`
- **THEN** stdout is those two names, one per line in sorted order, and the exit code is 0

#### Scenario: An empty slot set is not an error

- **WHEN** no template is stored and the user runs `encro completion --candidates template-name`
- **THEN** stdout is empty and the exit code is 0

#### Scenario: An unknown slot is rejected

- **WHEN** the user runs `encro completion --candidates nosuch-slot`
- **THEN** the command exits non-zero and the error names `nosuch-slot`

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

## MODIFIED Requirements

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
