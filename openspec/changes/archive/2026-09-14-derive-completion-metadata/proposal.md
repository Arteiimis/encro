## Why

Completion scripts already derive subcommand names, options, exclusions, and enumerated option values from the CLI's own registrations, but two kinds of metadata are still hardcoded or dropped: the set of path-taking options is a hand-maintained list in the emitter (`pathIds = {input, inputs, output, state_file, ffmpeg_path}`), and values of subcommand positionals never reach the scripts at all. Consequences today: `encro organize --model-dir <TAB>` offers nothing (not even shell-native file completion, which the glue actively suppresses), and `encro completion <TAB>` falls through to filename completion instead of offering `bash`/`powershell` even though the `shell` positional declares `cfg::Members`. Every future option or positional with a path or enumerated value silently inherits the same gaps unless someone remembers to edit the emitter.

## What Changes

- Add a declarative `cfg::Path{}` option token that registers the option's long name as path-taking; the emitter builds `pathIds` from this registry instead of the hardcoded literal. Path delegation becomes declaration-driven: a new path option completes files by construction.
- Annotate the existing path options (`--input`, `--inputs`, `--output`, `--state-file`, `--ffmpeg-path`) and organize's `--model-dir` with the token. Behavior for the existing five is unchanged; `--model-dir` gains shell-native directory completion.
- Record positional enum values at registration time: `cfg::Members` on a positional (no long name) stores its legal values in a pointer-keyed registry entry instead of dropping them. The completion model gains per-scope positional candidate lists.
- Both generated scripts (bash and PowerShell) offer enum candidates at subcommand positional slots: `encro completion <TAB>` completes `bash`/`powershell`, order-independent of flags (`encro completion --install <TAB>` completes the same). Slot detection skips flags and words that are values of a preceding value-taking option, so `encro organize --model-dir X <TAB>` still lands on organize's `dir` positional exactly as it does today. A prefix with no match suppresses file fallback, matching enum option behavior. Path positionals (preview's `original`/`encoded`, organize's `dir`) keep delegating to shell-native file completion; slots beyond a subcommand's positional count offer nothing.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `shell-completion`: the path-delegation requirement changes from a fixed enumeration of path options to declaration-driven coverage (any option marked path-taking delegates); a new requirement covers completing enumerated values at subcommand positional slots.

## Impact

- `src/cmd/option_specs.h`: new `cfg::Path` token; `cfg::Members` gains a positional branch calling the new registration.
- `src/cmd/completion_registry.{h,cpp}`: new `recordPath` (long-name keyed) and positional-candidates entries (option-pointer keyed, mirroring the existing config-store pointer capture).
- `src/cmd/completion_emitter.{h,cpp}`: `CompletionModel`/`ScopeInfo` gain positional candidates; `pathIds` built from the registry; bash and PowerShell glue gains the positional-slot branch.
- `src/cmd/cmd.cpp`: annotate the path options with `cfg::Path{}` — `--input`, `--inputs`, `--output` (both the main IO declaration and preview's), `--state-file`, `--ffmpeg-path`, and organize's `--model-dir`.
- Tests: `tests/cmd_completion_registry_tests.cpp`, `tests/cmd_completion_emitter_tests.cpp`, and real-shell TAB probes in `tests/cmd_completion_smoke_tests.cpp` gain coverage; main-spec sync at archive time.
