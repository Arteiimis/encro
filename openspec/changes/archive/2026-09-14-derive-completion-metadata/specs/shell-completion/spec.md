## MODIFIED Requirements

### Requirement: Path options delegate to the shell's native file completion

For options that take file or directory paths and are declared as path-taking in the CLI's own option definitions, the script SHALL delegate to the shell's native file-name completion instead of offering candidates from the CLI definitions. The path-taking set SHALL be derived from the option declarations (input, inputs, output, state file, ffmpeg path, and organize's model dir today), so an option registered as path-taking is covered without editing the completion generator. Subcommand positionals that take paths (preview's original/encoded, organize's dir) SHALL continue to fall through to the shell's native file completion; main-command positional inputs keep the existing main-scope behavior (subcommand names at the main position).

#### Scenario: Input path completes as a file

- **WHEN** the user types `encro -i ` and requests completion
- **THEN** the candidates are the shell's native file completions, not option names

#### Scenario: Organize model dir completes as a directory

- **WHEN** the user types `encro organize --model-dir ` and requests completion
- **THEN** the candidates are the shell's native file completions, not an empty set

## ADDED Requirements

### Requirement: Enumerated positional values complete at subcommand positional slots

For subcommand positionals whose legal values are enumerated by the CLI's own validation (for example the completion subcommand's shell argument), the script SHALL offer those legal values as candidates when the word being completed is the subcommand's next unfilled positional slot. Slot detection SHALL count only words that are neither flags nor values of a preceding value-taking option, so the candidates SHALL be identical regardless of where flags appear (`encro completion <TAB>` and `encro completion --install <TAB>` offer the same set) and words consumed as option values SHALL not advance the slot. A typed prefix with no legal match SHALL offer nothing and SHALL suppress the shell's native file fallback, matching enumerated option behavior. Subcommand positional slots without enumerated values (path positionals) SHALL keep falling through to the shell's native file completion, and slots beyond a subcommand's positional count SHALL offer nothing.

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
