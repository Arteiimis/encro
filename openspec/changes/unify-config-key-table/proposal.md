## Why

A configurable preference is declared in two places that nothing keeps in step: `configstore::kKeys` (`src/cmd/config_store.cpp:27`) lists all 17 keys with a hand-written `JsonKind`, and each option that backs one carries a `cfg::ConfigKey{"…"}` token (`src/cmd/cmd.cpp:557` for `model-dir`, `:757`–`:956` for the other 16). The token carries only the name, so the JSON type `config.json` is written with comes from the table while the value's real type comes from the option's binding — change a binding (int → string) and `save()` quietly writes a different JSON type.

Key → option also lives in a process-global mutable map (`configstore::configKeyRegistry()`, `config_store.cpp:67-73`) whose raw `CLI::Option*` values are half the reason `buildAppTree` leaks the whole `CLI::App` for the process lifetime (`cmd.cpp:1076`; documented as deliberate at `cmd.h:96-98`); `config_command.cpp:20-24` reaches into that map after parsing to print a built-in default. And `cfg::ConfigKey`'s functor (`option_specs.h:156-164`) additionally copies key → long name into `completion_registry`'s own map (`completion_registry.cpp:10`), which the emitter reads at `completion_emitter.cpp:188`.

Why now: the config surface is a documented user contract (README precedence; `tests/config_store_tests.cpp:170` asserts the canonical file order), and every new preference option currently has to be added twice, with a type that can drift.

## What Changes

- **One registration-time key table.** `registerOne` (`option_specs.h`) derives the `JsonKind` from `Spec::binding_type` — `bool` → Boolean, arithmetic (including `std::optional` thereof) → Number, otherwise String — and, for a spec carrying `cfg::ConfigKey`, records an entry holding: key, derived kind, long name, built-in default (`option->get_default_str()`), and a **by-value copy of the option's validator list**. No `CLI::Option*` outlives registration in the config path.
- **The validator copy is the whole validation contract.** In CLI11 2.7.2 `transform(...)` *is* a validator: it wraps the function in a `Validator` and inserts it at the front of the option's validator list (`CLI/impl/Option_inl.hpp:151-163`). So `configstore::validateValue`'s behavior — canonicalize, then run the option's checks, then the kind type check — is a walk of that one list, and copying it preserves canonicalization. Verified against the current binary: `config set color ALWAYS` stores `"always"` (the `color` option carries `cfg::Transform`), while `config set preset P5` and `config set output-format MP4` are rejected (those options carry only `cfg::Members`).
- **The canonical file order stays one literal list of key names** (`std::to_array<std::string_view>({…})`), because the file order is a contract rather than a derivation: `model-dir`'s spec sits inside the organize subcommand (`cmd.cpp:557`) yet is written last. Assembly is the pure function `configstore::assembleKeyTable(order, entries) -> eh::Result<KeyTable>`; a key without a token, a token without an order entry, or a duplicate key fails the parse with a named error through `CmdParseResult::error` (the channel `injectConfigDefaults` already uses).
- **The table is owned by the parse result** and travels with it: `CmdParseResult` holds it, `runConfigCommand(CmdParseResult const&)` reads it (it already receives the result), and `configstore::configKeyRegistry()` / `captureConfigKey` are deleted. `config_command.cpp`'s `builtinDefault` reads the captured default string instead of `option->get_default_str()`, and the key walkers that need the kind (`config list`, `save()`) read the table instead of the literal array.
- **Completion stops keeping a second copy**: the `cfg::ConfigKey` functor no longer records into the completion registry; `buildAppTree` records key → long name from the assembled table, so the emitter keeps working unchanged while the table becomes the single source of the key set.
- **Tests**: `config_store_tests.cpp:142`'s `keys().size() == 17` guard is deleted — with one source it can no longer catch a drift; kind-derivation cases for the three binding classes replace it, alongside the assembler's four cases (one per mismatch plus a clean input yielding canonical order). The canonical-order assertion (`:170`) stays, and the validator-order case (`:199`) is re-pointed at the new API with its named failure mode intact.

No behavior change: `config list/get/set/unset/path` output, stored JSON, built-in defaults, validation acceptance, and the completion candidates are identical. One asymmetry disappears by construction: a key can no longer be "known to the table but missing from the registry" (today `validateValue` returns "valid" for such a key, `config_store.cpp:84-86`), because assembly rejects that state at startup.

## Capabilities

### New Capabilities

None — this moves an existing key set behind one owner; it adds no observable behavior.

### Modified Capabilities

None — `user-config` specifies the file, precedence, and the validation-by-CLI-rules behavior, all of which stay; the key table, the registry, and ffprobe-style internals are not spec surfaces. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `refactor-long-param-lists` / `remove-immer-simplify-locks` / `reduce-over-engineering`.

## Impact

- `src/cmd/config_store.{h,cpp}` — `KeyDef` gains long name, built-in default and validator copies; `keys()`/`isKnownKey`/`jsonKindOf`/`validateValue`/`save` operate on a table value (the writer needs key → kind, `load`'s unknown-key check needs the key set, so `load` and `injectConfigDefaults` take it too); `configKeyRegistry`/`captureConfigKey` deleted; the canonical order list and `assembleKeyTable` live here
- `src/cmd/option_specs.h` — `registerOne`/`registerAll` derive the kind and record entries; `cfg::ConfigKey` becomes a name-only marker
- `src/cmd/cmd.{h,cpp}` — `CmdParseResult` owns the table, `buildAppTree` assembles it and reports mismatches through `result.error`; the 17 tokens stay where they are
- `src/cmd/config_command.cpp` — reads the table from the parse result; `builtinDefault` from the captured default
- `src/cmd/completion_registry.{h,cpp}` / `completion_emitter.cpp` — **no code change**: their config-key map is filled from the assembled table by `buildAppTree` instead of by the token functor
- `tests/config_store_tests.cpp` — the `captureConfigKey` setup, the `:142` guard, and the `save()`/`keys()`/`load()` call sites move to the table built through `testutils::parseArgs`; the config-command and completion suites keep their setups and stay green
- No new files, no new dependencies; the CLI surface and `config.json` format are untouched.

**Explicitly out of scope:** the leaked `CLI::App` and `CmdParseResult::helpApp_`. Capturing the config side by value removes that half of the leak's justification, but the completion registry still stores `CLI::Option const*` for positionals (`completion_registry.h:35`), so the lifetime question belongs to a separate change about the CLI/completion lifecycle.
