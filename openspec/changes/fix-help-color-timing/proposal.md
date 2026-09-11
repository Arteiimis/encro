## Why

Help output ignores the effective color mode. `CmdParseResult::helpText` is rendered to a final string inside `commandLineInit` (during `app->parse`, at `CLI::CallForHelp` handling), but the global terminal color mode is only configured afterwards in `prelude::initStartup` via `terminal::configureFromColorString(cmd.color)`. At render time the mode is still the process-initial `auto`, so on a real terminal `encro -h` prints ANSI colors even when the user configured `color = never` (config file) or passed `--color never`. The same applies to every help-flag path: the subcommand helps (`preview -h`, `organize -h`, `config -h`, `completion -h`) and bare `encro config` / `encro completion`, which print the pre-rendered string.

## What Changes

- Help text is no longer materialized at parse time. `CmdParseResult` carries a lazy help renderer; the string is produced when a consumer actually prints it (after the color mode is configured).
- All help-printing sites (`app_entry`, bare `config`, bare `completion`) render through the lazy accessor, so CLI `--color`, config `color`, `NO_COLOR`, and TTY detection all decide coloring at print time.
- Known limitation, accepted: bare `encro config` and bare `encro completion` still render under the `auto` mode. They return from the probe parse, which deliberately skips config injection (a corrupt config file must not break them), so their `result.color` stays `auto`. Every help-flag path re-parses with injection and honors the configured mode.
- No change to which text is rendered, to help layout, tiers, streams, or exit codes — only the rendering moment moves.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `cli-help-layout`: new requirement — help-flag output (main brief/full tiers, subcommand helps) honors the effective color mode (CLI `--color` > config `color` > `auto`) at print time; bare `config`/`completion` are explicitly outside the requirement.

## Impact

- `src/cmd/cmd.h` / `src/cmd/cmd.cpp`: `helpText` field becomes a lazily-invoked renderer (eight assignment sites in `buildAndParse`: post-parse main help, config/completion got-subcommand paths, four `CallForHelp` catch branches, `ParseError` catch).
- `src/app/app_entry.cpp`, `src/cmd/config_command.cpp`, `src/cmd/completion_command.cpp`: switch from `cmd.helpText` to the renderer call.
- Tests referencing `result.helpText` (~39 sites across `tests/cmd_cmd_tests.cpp`, `tests/cmd_completion_command_tests.cpp`, `tests/cmd_help_tiering_tests.cpp`, `tests/cmd_config_tests.cpp`, `tests/cmd_organize_tests.cpp`) switch to the accessor call; the existing color-invariance test is restructured to materialize each render while its mode is still active; new regression tests cover configure-after-parse, CLI `--color never/always -h`, and config-file `color = never` + `-h`.
- No spec-level change to `user-config` (injection semantics unchanged) or `console-output-conventions` (prefix/stream rules unchanged).
