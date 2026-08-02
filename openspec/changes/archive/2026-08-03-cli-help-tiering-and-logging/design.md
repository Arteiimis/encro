## Context

Current CLI (src/cmd/cmd.cpp) builds ~30 options from constexpr `CmdFlagDef` arrays (4 groups), registered via a `registerFlag` lambda; a custom `makeHelpFormatter` closure renders desc + usage + 4 groups, wrapping at `resolveHelpTextLayout()` widths. `-h/--help` is registered with `app.set_help_flag("")` so the app owns it; `applyMap` converts to `CmdParseResult` with `count() > 0` checks. Logging (src/logging/setup.cpp) is fully disabled when neither `-v` nor `--log-json` is given; `-v` enables the file sink, `-e` the console sink, and `verbose && verboseEcho` also kills progress bars (src/video/video_batch_execution.cpp:317). `prelude::setupLogging` prints the log-file hint and warns when `-e` is used alone. `failWithHint` (src/app/app_entry.cpp:84) chooses between `LOG_ERROR` and `terminal::println(Error)` based on whether a log file exists. `Store::initialize` (src/core/job_state_store.cpp:15-51) silently discards a mismatched saved state unless `--resume` was given.

## Goals / Non-Goals

**Goals:**
- Two-tier help (`-h` brief / `-hh` full) with a single source of truth for option definitions
- Always-on file logging; `-v` becomes console echo; remove `-e` and `--flat`
- User-visible warning when automatic resume discards a mismatched state
- Zero behavior change for parsed values (all `applyMap` entries remain `count() > 0`)

**Non-Goals:**
- Renaming `--force-conflict-handling` (breaking; deferred)
- Fixing `-i`/`-I` twin flags, `-o` magic aliases, ffmpeg letter clashes (deferred to v2)
- Making release builds emit TRACE/DEBUG (compile-time stripping stays)

## Decisions

### D1: `-hh` detection via option count
The help flag is a plain `add_flag`; CLI11 parses `-hh` as `-h` twice, so `helpOpt->count() >= 2` selects the full tier (verified in a spike). The formatter closure captures the help option pointer and reads `count()` at render time. Parse-error paths naturally render the brief tier (`count() == 0`).

### D2: Advanced-flag marking in the definition array
Add a `bool advanced` member to `CmdFlagDef`; the formatter skips advanced options when rendering the brief tier. The skip test compares the option's `get_lnames().front()` (first long name) against the set of advanced long names — **not** `def.name`, because `formatOptionName` renders `--long,-s`, i.e. the reverse order of `def.name`. The advanced set is derived from the same `CmdFlagDef` arrays so help stays consistent: `-v`, `--log-json`, `-F`, `--color`, `-I`, `--state-file`, `--force-conflict-handling`, `-x`, `--preset`.

### D3: Logging restructure
`LogConfig` drops `verboseEnabled`/`verboseEchoEnabled`; a single `echoEnabled` (= `cmd.verbose`) remains alongside `jsonEnabled`/`colorsEnabled`. In `logging::setup`: the early `set_level(off)` branch and the `if (config.verboseEnabled)` gate on the file sink are deleted — the file sink is always created, the console sink when `echoEnabled`. Logger levels stay `debug` (unchanged: release builds already strip TRACE/DEBUG at compile time). `--log-json` now produces `.log` **and** `.ndjson` companions; that is intended.

### D4: Startup hint and error display
`prelude::setupLogging` always prints `Log file: <path>` when the file was created (no `!verboseEcho` condition anymore). The exit-time `printVerboseLogDirHint` is deleted as redundant, and `StartupContext.verboseLogFilePath` is dropped (its only other consumer, `failWithHint`, no longer needs it). `failWithHint` is keyed on `cmd.verbose` instead of log-file existence: no `-v` → `terminal::println(Error)` + `LOG_ERROR` (file); `-v` → `LOG_ERROR` only (console sink shows it once, no duplicate).

### D5: Progress-bar gating
`video_batch_execution.cpp:317` becomes `if (ctx.config.verbose)`; the message is reworded to `Verbose output enabled: progress bars are disabled.` `config.verboseEcho` is removed from `AppContext`/`CmdParseResult`/`config_builder`.

### D6: Mismatched-state warning
`Store::initialize(config, restart)` gains an out-param (e.g. `bool* discardedMismatched = nullptr`) set when a state existed, did not match, and was discarded without an explicit `--resume`. `pipeline::ensureJobState` passes a local, prints `terminal::println(Warning, ...)` (console — `LOG_WARN` alone would stay invisible on console) and logs the same fact. Behavior of the discard itself is unchanged.

### D7: Default-value display
`CmdFlagDef` gains an optional `defaultDisplay` string; `registerFlag` calls `opt->default_str(...)` when set, for `-q` (`2`), `--crf` (`28`), `--preset` (`auto`). Parsed results are unaffected because every `applyMap` entry checks `count() > 0`. Caveat: CLI11 may reject `default_str` on options with `expectedMin == 1`; if so, those three defs drop to `expectedMin == 0` — semantically neutral for count-based parsing.

### D8: Flag removals and help text
`--flat` and `-e/--verbose-echo` defs, applyMap entries and `CmdParseResult` fields (`flat`, `verboseEcho`) are deleted. Description strings updated in the def arrays: `-h` ("show help; use -hh to show all options"), `--resume`, `--force-conflict-handling`, `--keep` (adds `(default: flatten)`). Usage line 4 becomes `encro -h | -hh | --version`; the brief tier appends `Run 'encro -hh' to view all options.`

## Risks / Trade-offs

- [Breaking: `-e` removed, `-v` semantics flipped] → documented in help text; old `-v` users see echo + no progress bars, which is the intended new behavior
- [`default_str` + `expectedMin=1` interaction] → fall back to `expectedMin=0` (D7); covered by help-output tests
- [Double `.log` + `.ndjson` when `--log-json`] → intended; retention (keep 10) unchanged
- [Release logs are INFO+ only] → unchanged from today's `-v`; no action
- [Formatter filter must match CLI11's name order] → filter by `get_lnames().front()` (D2); covered by brief/full output tests

## Migration Plan

No data migration. CLI migration notes: `-e` → use `-v`; `-v` previously wrote a file only, now echoes to console (file writing is the default for everyone).

## Open Questions

None — deferred decisions (flag renames, `-i`/`-I`, `-o` aliases) are recorded as Non-Goals, not unknowns.
