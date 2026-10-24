# Proposal

## Why

`encro template`'s names are the one part of the command surface a user still has to retype: `encro -T <TAB>` offers nothing at all, and `encro template show/delete/save/unset <TAB>` falls through to the shell's native *file* completion, so the slot suggests `.json` files from the current directory instead of the templates that exist. The generated completion scripts cannot cover it as they stand: candidates are captured from the declarative option definitions at registration time and embedded in the script as a frozen table, while template names are files resolved at run time from a directory that `ENCRO_CONFIG` can relocate. Re-emitting the script when a template is saved does not fix it either — bash installs through bash-completion's lazy-load directory, which a running shell sources once, and baking names in would make emission depend on the user's template store, which the completion contract forbids.

## What Changes

- Completion scripts gain a **runtime candidate channel**: a value or positional slot can be marked in the emitted model as backed by live data, carrying a provider id, and the generated bash/PowerShell glue asks the binary for that slot's candidates at TAB time instead of reading only the embedded static tables.
- `-T/--template <name>` and the `template` subcommand's `<name>` positional (`show`, `save`, `unset`, `delete`) offer the stored template names, resolved from the templates directory at completion time.
- `encro template unset <name> <key>` offers the templateable keys as an ordinary static positional candidate set, taken from the compile-time template key table, so that slot costs no extra process.
- New machine channel `encro completion --candidates <slot>`: prints one candidate per line and exits 0, prints nothing and exits 0 when the slot is currently empty, and exits non-zero with an error naming the slot when the id is unknown (a version-skew guard). It resolves the templates directory exactly as `template path` does, never parses a template file, and reads no stored config values.
- Emission stays byte-identical and independent of the template store: the emitted script carries provider ids, never template names.
- Not changing: the static candidate tables (option names, enumerated values, config keys, exclusion hiding, path delegation), the install/uninstall locations, and the behavior of every other completion slot.

## Capabilities

### New Capabilities

<!-- none -->

### Modified Capabilities

- `shell-completion`: scripts resolve dynamically-backed candidate slots — the `-T/--template` value and the `template` name positional — at completion time through a provider channel; the `template` key positional gains static candidates; the enumerated-value requirement is restated so a provider-backed free-text slot may offer candidates while every other numeric and free-text option still offers none; and the emission determinism and template-store independence are restated in those terms.

## Impact

- **Code**: `src/cmd/completion_registry.{h,cpp}` (provider recording beside the existing candidate/numeric/path markers), `src/cmd/option_specs.h` (provider tokens next to `cfg::Members`/`cfg::Path`), `src/cmd/completion_emitter.{h,cpp}` (provider ids in the model and in both glues), `src/cmd/completion_command.cpp` (the `--candidates` action), `src/cmd/cmd.{h,cpp}` (one new option field on `CmdParseResult`, provider tokens on `-T/--template` and the template name positional, key candidates recorded when the template table is assembled).
- **Behavior**: TAB on a template-name slot costs one `encro` invocation (~50-120 ms measured on `template path` on Windows); every other slot keeps its current spawn-free behavior. No run-time CLI contract changes; `encro completion --help` gains one option line, and a script emitted by an older build simply has no dynamic branch.
- **Compatibility**: upgrading the binary alone does not enable the new slots — an installed script is a generated snapshot, so `encro completion <shell> --install` must be re-run, which the scripts and install output already instruct.
- **Tests**: a registry unit test for provider capture, an emitter model test for the dynamic slot and the emitted glue, a `completion` command test for the channel (names, empty set, unknown slot), and one `[completion][smoke]` case driving real bash/PowerShell against a seeded template store.
- **Documentation**: the README "Shell completion" paragraph, which today describes every candidate as embedded in the installed script.
