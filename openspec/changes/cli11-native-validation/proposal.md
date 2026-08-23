## Why

The CLI layer underuses CLI11 (v2.6.2): value validation (enums, ranges, aliases), option binding, subcommand checks, and error messages are hand-rolled in `src/cmd/` (~640 lines) even though CLI11 provides native `IsMember`/`CheckedTransformer`/`Range`/`needs`/`excludes`/`RequiredError`/`CallForHelp`. This duplicates logic (`CmdFlagDef` arrays vs. a parallel `applyMap` keyed by the same strings), leaves dead code (`excludesDesc` is collected but never applied; hand-rolled "option requires a value" errors on options that CLI11 already enforces), and makes the `preview` subcommand bypass native help/validation entirely.

## What Changes

- **Value validation moves into CLI11**: enum checks (`-f`, `--force-conflict-handling`, `--color`, `--preset`), alias mapping (`-t vid|pic` via `CheckedTransformer`), and range checks (`-q` 2-31, `--crf` 0-51, `--min-vmaf` 0-100, `-j` >= 1, preview `--start`/`--duration` >= 0) run at parse time with native error messages.
- **Options bind directly to `CmdParseResult` fields**: the `CmdFlagDef` arrays, `ResultSetter`/`applyMap`, `optRegistry`, and deferred `PendingExclusion` machinery are deleted; ~28 options are registered by hand, 2-3 lines each. `CmdParseResult` stays as-is (tests construct it directly).
- **Conflicts/dependencies declared natively**: `excludes()` (including positional-vs-`-i`/`-I`, `--dry-run` vs `--crf`) and `needs()` (`--image-quality` requires `--compress`). The dead `excludesDesc` field is removed; native `X excludes Y` messages are used. **BREAKING:** error message texts change to CLI11's native style across the board.
- **`preview` subcommand uses native CLI11**: native help flag + `CallForHelp` dispatch (exit 0), `original->required()` (replaces the hand-written missing-argument error), numeric checks, direct field binding, and a small preview-specific formatter reusing the existing render helpers — the hand-written `buildPreviewHelpText` is deleted, so help is generated from option definitions.
- **Kept as-is**: the main `-h`/`-hh` two-tier help, the main colored `formatter_fn`, usage lines, option groups, filesystem validation, output-alias resolution, and cross-option *value-conditional* checks that CLI11 cannot express (`--compress` only with `--type picture`, multi-input only for `video`, multi-input incompatible with `--pack-only`).
- Tests: error-message assertions updated to native texts; parse-time validation covered in `cmd` tests.

## Capabilities

### New Capabilities
- `cli11-native-validation`: CLI argument value validation (enums/aliases/ranges), option exclusion/dependency constraints, and parse-time error reporting are performed by CLI11 with native error messages; options bind directly to typed fields.

### Modified Capabilities
- `cli-positional-input`: the positional-vs-`-i`/`-I` conflict is now rejected by CLI11's native exclusion check with a native-style message naming both options, instead of the hand-written `buildConfig` message.

## Impact

- `src/cmd/cmd.cpp`: registration/binding rewritten; apply-map and def-array machinery deleted; `CallForHelp` dispatch added; preview subcommand + mini formatter.
- `src/cmd/config_builder.cpp`: hand-written enum/range/alias/exclusion checks removed; remaining cross-option validations kept.
- `src/cmd/cmd.h`: `CmdParseResult` unchanged.
- `src/app/prelude.cpp`: `--color` no longer reaches its error path with an invalid value (parse-time check), error branch becomes unreachable.
- Tests: `tests/cmd_cmd_tests.cpp`, `tests/cmd_config_builder_tests.cpp` (native message assertions); `tests/cmd_help_tiering_tests.cpp` must stay green unchanged (help layout contract).
- Dependency: CLI11 v2.6.2 already in use, no version change.