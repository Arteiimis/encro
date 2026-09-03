# Proposal: add-shell-completion

## Why

The CLI has grown to 40+ option names across two subcommands and four option groups, yet nothing the terminal offers completes any of it: every option name, enum value, and config key must be typed from memory and verified against `-h`. CLI11 (our parsing library) ships no completion support, so encro must generate and install its own completion scripts. The declarative `OptSpec` tables already hold everything needed (names, descriptions, `Members`/`CheckedTransformer` legal values, config-key registry), making this cheap to build now and easy to keep in sync.

## What Changes

- New visible `completion` subcommand: `encro completion <powershell|bash>` prints a completion script generated from the live CLI11 app tree, so completed names can never drift from the registered CLI.
- Generated scripts embed static candidate values at emission time: enum sets from `Members`/`CheckedTransformer` and config keys from the config-key registry. No per-keystroke process callback.
- Generated scripts filter candidates with the CLI's own mutual-exclusion graph (`Option::get_excludes()`), so a flag that conflicts with an already-typed one (e.g. `--resume` vs `--restart`, `config --set` vs `config --get`) is no longer offered.
- Assisted install/uninstall: `encro completion <shell> --install` wires the script up idempotently (PowerShell: script file plus dot-source entries in the PowerShell profile(s); bash: bash-completion lazy-load directory, falling back to a `~/.bashrc` source block). `--uninstall` removes both cleanly.
- Non-goals: cmd.exe/Clink, zsh, and fish shells; `Needs`-based candidate hiding (parse is order-independent, hiding would block valid input orders); dynamic per-keystroke completion protocol (`__complete`); `-h`/`-hh` tier distinction inside completion (all options are completed).

## Capabilities

### New Capabilities

- `shell-completion`: `encro completion <shell>` script emission — supported shells, what the generated scripts must complete (option names in short/long/negated forms, subcommands, enum candidate values, config keys), exclusion-aware candidate filtering, and deterministic output.
- `completion-install`: the `--install`/`--uninstall` lifecycle for generated scripts — per-shell install locations, profile wiring, idempotency, and clean rollback.

### Modified Capabilities

- `cli-help-layout`: the main-help commands section gains the `completion` subcommand entry alongside `preview` and `config`.

## Impact

- **Code**: new completion module (emitter, per-shell script templates, install/uninstall logic); `completion` subcommand registration in `src/cmd/cmd.cpp` with its own help formatter, mirroring `preview`/`config`; candidate-value capture side channel in `src/cmd/option_specs.h` (mirroring the `ConfigKey` precedent).
- **APIs**: CLI11 v2.7.2 public API only (`get_options`, `get_excludes`, `get_subcommands`); no new third-party dependencies.
- **Tests**: unit tests for emitted-script content per shell, excludes-graph symmetry, and install/uninstall idempotency and rollback using `TempDir` with redirected `USERPROFILE`/`HOME` and a known-folder probe; optional `[smoke]` tests that source the generated scripts under real `bash`/`powershell` when present on PATH.
- **Docs**: README gains a short "enable completion" section pointing at the install command per shell.
