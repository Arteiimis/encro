# Proposal: persistent-user-config

## Why

Every encro invocation must repeat its full set of preferences on the command line (encoder quality, parallelism, ffmpeg path, packing behavior, ...). Users who consistently run with the same values have no way to persist them, so each command line grows long and error-prone. A user-level config file with an `encro config` subcommand lets preferences be stated once.

## What Changes

- Add a user-level JSON config file holding preference defaults for a fixed set of keys (see below). The file is written pretty-formatted (indented, stable key order) so it stays hand-editable and diff-friendly.
- Config file location (first match wins): `$ENCRO_CONFIG` env var override; otherwise `%LOCALAPPDATA%\encro\config.json` (fallback `%APPDATA%\encro\config.json`) on Windows, `$XDG_CONFIG_HOME/encro/config.json` (fallback `~/.config/encro/config.json`) on POSIX. A missing file means "no config" with zero effect on behavior.
- Precedence for every configurable option: command line > config file > built-in default.
- Configurable keys (named after the CLI long option):
  - Value options: `color`, `output-format`, `force-conflict-handling`, `jobs`, `ffmpeg-path`, `image-quality`, `crf`, `min-vmaf`, `preset`, `video-codec`
  - Boolean flags: `yes`, `pack`, `keep`, `compress`, `recursive`, `folder-summary`
- Explicitly NOT configurable (task-scoped, stays invocation-only): inputs, `-o/--output`, `--state-file`, `-t/--type`, `--resume`/`--restart`, `--dry-run`, `-z/--pack-only`, `-w/--overwrite`, `--verbose`/`--log-json`/`--full-progress`, and all `preview` options.
- New `encro config` subcommand with actions `list`, `get`, `set`, `unset`, `path`:
  - `set` validates values with the same rules as the CLI (members/ranges) and rejects unknown keys; the whole file is rewritten in pretty form after each `set`/`unset`.
  - `list` shows every known key with its current value and source (`config` or `default`); `get` prints one key's value; `unset` removes a key (back to default); `path` prints the resolved config file location.
  - Config actions never require an input path and never enter the encode/preview workflows.
- Each persistable boolean flag gains a negation form (`--no-yes`, `--no-pack`, `--no-keep`, `--no-compress`, `--no-recursive`, `--no-folder-summary`) so a config-persisted "on" can be turned off for a single run; help renders these collapsed as `--[no-]pack`.
- Values read from the config file flow through the same parse-time validators as command-line values, so a hand-edited invalid value (e.g. `crf: 99`) fails with the same native-style error as an out-of-range CLI value.
- Config key/values are also recorded in `-h`/`-hh` default displays (`(=...)` shows the effective default once config is applied).

## Capabilities

### New Capabilities

- `user-config`: persistent user-level configuration — config file resolution, precedence rules, the configurable key set, the `encro config` subcommand (list/get/set/unset/path), `ENCRO_CONFIG` override, negation flags for persistable flags, and config-value validation behavior.

### Modified Capabilities

- `cli-help-tiering`: the usage section gains an `encro config` invocation line, and option-name rendering collapses paired negation flags into the `--[no-]x` form in the option tables.
- `cli11-native-validation`: the "Main help output stays byte-stable" requirement is rescoped so help output changes only via explicitly declared capability specifications (the config usage line, collapsed negation rendering, and config-driven default displays introduced here are the declared exceptions).

## Impact

- **Code**: `src/cmd/cmd.cpp` / `cmd.h` (config subcommand registration, negation flags, default injection before parse, help name collapsing, usage lines); new config store module (file resolution, load/save via boost::json, key table); `src/cmd/config_builder.cpp` (no merge logic expected there if injection happens at registration); `src/app/app_entry.cpp` (config-subcommand early dispatch before `buildAppConfig`/toolchain).
- **Help**: main help usage lines and the six persistable flag entries; new `encro config -h` output rendered from its own option definitions (same pattern as `preview`).
- **Tests**: unit tests for the config store, precedence, key validation, negation flags, and help rendering; e2e tests for the config subcommand and precedence in real runs. All test entry points (unit fixture + e2e harness) must set `ENCRO_CONFIG` to an isolated path so developer/user config files can never leak into test results.
- **Dependencies**: none new (boost::json already in use for job-state and probe-cache persistence).
