# Design: persistent-user-config

## Context

All options are defined declaratively in `src/cmd/cmd.cpp` as `OptSpec` tuples and bound directly to `CmdParseResult` fields. Options advertise defaults via `cfg::RequiredDefault`/`DefaultValue` `default_str`s (the source of the help `(=...)` display), but an absent option leaves its bound field untouched at the C++ default; several fields are plain `std::string`s where "user passed" and "built-in default" are indistinguishable after parse. Post-parse field inspection is therefore not a viable merge point — and the effective-default help display (`(=23)` after `config set crf 23`) requires influence before parse renders help anyway.

Existing persistence precedents: probe cache (`resolveCommonLogDir().parent_path() / "probe-cache.json"`, env override `ENCRO_PROBE_CACHE`), job state (`job-state.json`, boost::json). boost::json is already a dependency; JSON parsing/serialization patterns exist in `probe_cache.cpp` and `job_state_store.cpp`.

Help output is rendered by a fully custom formatter (`makeHelpFormatter` / `formatOptionName` in `cmd.cpp`); option names displayed are derived from `opt->get_lnames()`, so rendering can be reshaped without touching CLI11's default formatter.

## Goals / Non-Goals

Goals:
- One user-level config file; CLI > config > built-in precedence with no per-field merge logic scattered in `config_builder.cpp`.
- `config set` validates with the same rules the CLI enforces (single source of truth).
- Help reflects effective defaults (`(=23)` after `config set crf 23`).
- Missing config file and missing keys are zero-impact; all existing behavior and tests stay green unless they assert on the changed help bytes.

Non-Goals:
- No directory/project-level config layering, no import/export, no `config edit`, no config schema versioning.
- No negation forms for non-persistable flags (`--resume`, `--dry-run`, ...): absence already means false.
- No watch/reload; config is read once per process start.

## Decisions

### D1: Precedence via default-string injection before parse

Load the config inside `commandLineInit` before CLI parsing; for every configurable key present in the config, set the corresponding option's `default_str` to the config value and call `force_callback()` on it. In CLI11 (v2.7.2, verified against the vendored source) an absent option's result callback runs only when forced, so `default_str` alone never reaches the binding; with `force_callback`, `run_callback` feeds `default_str` through the same validation and conversion path when results are empty. The result: values not supplied on the command line receive the config value, supplied values win, validators run on applied defaults, and the custom help formatter's `(=default_str)` display automatically shows effective defaults (spec requirement "Effective defaults shown in help default displays" falls out for free). Options without a config value are left untouched, preserving today's behavior exactly.

Alternatives rejected:
- Post-parse merge keyed on `option->count()`: needs new plumbing to carry per-option counts out of `commandLineInit`, scatters merge logic across `buildConfig`, and cannot influence help text because help is rendered during parse. The help-display requirement alone disqualifies it.
- Writing config values into `CmdParseResult` fields before registration and using them as `default_str`: same direction, but only the option-level `default_str` update after registration is needed; fields keep their built-in C++ defaults.

Verify early (first task of implementation): with CLI11 2.7.2, that flag `default_str("true"/"false")` + `force_callback()` defaults the six boolean flags, that the `{false}` per-name suffix syntax (`"-p,--pack,--no-pack{false}"`) compiles and writes false only for `--no-pack`, and that App-level needs/excludes keep evaluating on command-line occurrences only (an injected default keeps count 0, so a config-set `image-quality` never demands `-c`). Fallback if flag defaults misbehave: post-parse merge for the six flags only (`count() == 0` → assign from config), keeping D1 for value options; the specs do not change.

### D2: New module `src/cmd/config_store.{h,cpp}`

Owns: config path resolution (`ENCRO_CONFIG` → `%LOCALAPPDATA%`/`%APPDATA%` fallback on Windows, `XDG_CONFIG_HOME`/`~/.config` fallback elsewhere — mirroring the env fallback chain in `logging/setup.cpp`, but resolving the config root, not the state root), load (missing file → empty; malformed JSON → error), save (pretty rewrite), and the key table.

Canonical in-memory form is the CLI's native currency: strings. On load, JSON values are stringified (`23` → `"23"`, `true` → `"true"`); on save, each key's declared JSON kind (number / boolean / string) drives typed output so files contain `"crf": 23`, not `"crf": "23"`.

Key table: one `ConfigKeyDef` array (`key`, bound CLI long name, JSON kind) shared by the merge (D1), `config set` validation (D3), `config list` (order + source), and save (canonical key order = table declaration order, grouped like the CLI help groups). The table lives in `config_store` and is filled/consulted via the registry described in D3.

`set`/`unset` rewrite the whole file from in-memory state; unknown keys found in a hand-edited file are warned about on load and dropped on rewrite (they were never valid). `config set` creates the parent directory on first write.

### D3: Validation reuse through the declarative option registry

`option_specs.h` is already declarative, so instead of duplicating members/range rules for `config set`, a static registry `key → validators` is populated during `commandLineInit` registration (a new optional `cfg::ConfigKey{"crf"}` token on the relevant `opt(...)` specs). The registry stores `std::shared_ptr<Validator>` copies captured at registration (via `Option::get_validator(i)`) — NOT `CLI::Option*` pointers, which dangle once `commandLineInit` returns because the `CLI::App` and its options are function-local. Validators (including `CheckedTransformer`/`Transform`, which are stored as validators) are standalone-applicable shared pointers, so `config set` validates a candidate value against exactly the rules the CLI enforces, with transformers canonicalizing the stored value. `Needs`/`Excludes` are App-level relations, not validators, and are naturally excluded — `config set image-quality 10` validates the value without requiring `--compress`.

Alternatives rejected:
- Re-parsing `encro --crf <value>` against a scratch app: dependency checks (`-q` needs `-c`) fail for reasons unrelated to the value.
- Duplicating rules in the key table: two sources of truth; drift means `config set` accepts values the CLI rejects.

Fallback if the validator plumbing proves awkward: mirror the rules in the key table (D2) and add a unit test cross-checking table rules against CLI parse outcomes for each key.

### D4: Config subcommand modeled on `preview`

`add_subcommand("config", ...)` with its own help flag and a `makeConfigHelpFormatter` mirroring `makePreviewHelpFormatter` (renders the subcommand's own options with the shared style helpers). Actions as mutually exclusive options (native `excludes`): `--list` flag, `--get <key>`, `--set <key> <value>`, `--unset <key>`, `--path` flag. `CmdParseResult` gains the config fields and a `config` flag set via `got_subcommand`. `appentry::run` dispatches to a `runConfigCommand` right after `handleParseAndHelp` — before `buildAppConfig` and `ensureToolchainReady` — so config actions never require inputs or ffmpeg. Bare `encro config` (no action) does not throw CallForHelp, so `parseAndPopulate` additionally stores the config subcommand's own help text in `result.helpText` when the config subcommand was matched; `runConfigCommand` prints it and exits 0, matching the preview help convention.

Subcommand name collision with positional inputs is already covered by the existing "Subcommand names take precedence" behavior (`cli-positional-input` spec) — no change needed there.

### D5: Negation flags and collapsed rendering

The six persistable boolean flags register dual names, e.g. `"-p,--pack,--no-pack{false}"` (plain `--pack` keeps flag semantics true; only the negation needs the explicit `{false}`). `formatOptionName` gains a collapse rule: when an option's long names contain both `x` and `no-x`, render `-p, --[no-]pack`. Update the help usage lines with the config invocation form. `tests/cmd_help_tiering_tests.cpp` goldens and any e2e help assertions are updated deliberately as part of this change (config isolated via `ENCRO_CONFIG`, so goldens still show built-in defaults).

### D6: Error reporting rides existing channels

Config load errors (malformed JSON) are reported through `CmdParseResult::error` — the existing parse-error path (native-style message, non-zero exit, help hint). Unknown-key warnings print via `terminal` to stderr before parse (logging is not yet set up at that stage; `terminal` is independent of it).

## Risks / Trade-offs

- [Config-injected defaults change parse semantics] → Spike test (task 3.1) pins flag `default_str` + `force_callback` behavior, the `--no-x{false}` syntax, and that needs/excludes keep evaluating on command-line occurrences only; documented fallback in D1/D3 keeps scope if they misbehave.
- [Config value injected as default changes help bytes per machine] → Accepted and spec'd: goldens run with `ENCRO_CONFIG` isolated, so checked-in expectations stay deterministic.
- [Concurrent encro processes writing config simultaneously] → Last-write-wins whole-file rewrite; acceptable for a single-user preference file (documented ceiling).

## Migration Plan

Purely additive: no config file is shipped, missing file is a no-op, and no existing flag changes meaning (`--no-x` forms are new names). Rollback is removing the subcommand and ignoring the file. The only intentional observable changes to existing behavior are the help usage lines, the six collapsed flag renderings, and `(=...)` displays reflecting config — each covered by updated goldens.

## Open Questions

None.
