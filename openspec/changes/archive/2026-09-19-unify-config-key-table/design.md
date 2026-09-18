## Context

See `proposal.md` — Why for the pain. The state that shapes the approach:

- `configstore::KeyDef` is `{std::string_view key; JsonKind kind}` (`config_store.h:25-28`), the literal `kKeys` holds 17 of them (`config_store.cpp:27-45`), and `keys()`/`isKnownKey`/`jsonKindOf`/`save()` read that one array (`:48`, `:52`, `:57`, `:252`).
- `configKeyRegistry()` is a function-static `std::map<std::string, CLI::Option*>&` (`config_store.cpp:67-69`), filled by `captureConfigKey` (`:73`) from `cfg::ConfigKey`'s functor (`option_specs.h:156-164`), and read by `validateValue` (`:84`) and by `config_command.cpp`'s `builtinDefault` (`:20-24`).
- `validateValue` (`config_store.cpp:81-112`) walks `option->get_validator(index)` / calls each / then type-checks Boolean and Number text.
- CLI11 2.7.2 keeps transforms *in* the validator list: `Option::transform(...)` wraps the function in a `Validator` and inserts it at the front (`CLI/impl/Option_inl.hpp:125-163`), which is why the config path canonicalizes at all.
- `registerOne` (`option_specs.h:186-198`) is the only place that sees both the binding type (`Spec::binding_type`) and the spec's `cfg` tuple; `registerAll` (`:228-234`) applies specs in registration order and already has the `CLI::Option*` for every spec.
- `CmdParseResult` is the object the options are bound to; it already carries the parse error channel (`cmd.h:93`, set in `buildAppTree` from `injectConfigDefaults`'s return, `cmd.cpp:1125-1126`, and returned early on at `cmd.cpp:1026`) and is passed to `runConfigCommand` (`config_command.h:11`, called from `app_entry.cpp:299`).
- Config-key token source order (from `cmd.cpp`): `model-dir` (`:557`), then `color` (`:757`), `yes` (`:770`), `output-format` (`:813`), `keep` (`:821`), `force-conflict-handling` (`:829`), `folder-summary` (`:836`), `recursive` (`:842`), `jobs` (`:877`), `ffmpeg-path` (`:891`), `compress` (`:898`), `image-quality` (`:905`), `crf` (`:914`), `min-vmaf` (`:922`), `preset` (`:936`), `video-codec` (`:944`), `pack` (`:956`). `model-dir` is first in the source yet last in the file; registration order only matches `kKeys` because `buildAppTree` happens to call the organize registrar last (`:1100`).
- Tests that pin today's contract: `tests/config_store_tests.cpp:142` (`keys().size() == 17`), `:170` (canonical order in the saved text), `:199-223` (validator order + canonicalization, built on a hand-made `CLI::App` and `captureConfigKey`), `tests/cmd_completion_registry_tests.cpp:98-108,140-157` (`recordConfigKey` / `configKeys()` / `longNameOfConfigKey`).

## Goals / Non-Goals

**Goals:**

- One owner of the key set: which keys exist, their JSON kind, their built-in default, their validation behavior, and their long name all come from a single registration-time table.
- The JSON kind cannot drift from the bound C++ type, because it is derived rather than declared.
- The config path holds no `CLI::Option*` after registration, so the "who keeps the pointers alive" question shrinks to the completion side.
- Mismatch between the token set and the canonical order fails loudly at startup instead of silently dropping a key from `config list`.

**Non-Goals:**

- **The leaked `CLI::App`.** `completion_registry`'s positional map (`completion_registry.h:35`) still stores `CLI::Option const*`, so the lifetime question stays open; this change only removes the config half of its justification.
- **Changing the canonical order.** `model-dir` keeps its last position in the file; the order list is a file-format contract that only the tests pin (`tests/config_store_tests.cpp:170`).
- **Fixing canonicalization asymmetry.** `preset`/`output-format` reject uppercase while `color`/`force-conflict-handling` accept it, because only the latter pair carries a transform (`cfg::Transform` / `cfg::CheckedTransformer`); that is today's behavior and stays (see Risks for the follow-up).
- **New config keys, new CLI flags, or a `config.json` format change.**
- **Rewriting `completion_registry`'s other maps** (`optionValues`, `pathOptions`, `positionalOptions`) — only the config-key map changes hands.

## Decisions

**D1 — The table holds values, not pointers.**

```cpp
namespace configstore {

struct KeyDef {                 // grows from {key, kind}
  std::string_view key;         // token name (string literal)
  JsonKind kind;
  std::string longName;         // "--crf"
  std::string builtinDefault;   // option->get_default_str() at registration
  std::vector<CLI::Validator> validators;  // copied in option order (transform first)
};

struct KeyTable {
  std::vector<KeyDef> keys;     // canonical order
  auto find(std::string_view key) const -> KeyDef const*;
  // Requires a key present in the table: every caller rejects unknown keys first.
  auto validate(std::string_view key, std::string& value) const
    -> std::optional<std::string>;
};

// Pure: orders `entries` (registration-time KeyDefs, registration order) by
// `order` and rejects mismatches. No app, no `CLI::Option*`, no globals.
auto assembleKeyTable(std::span<std::string_view const> order,
                      std::span<KeyDef const> entries) -> eh::Result<KeyTable>;
}
```

`CLI::Validator` is copyable (it wraps a `std::function` plus name/description) and the built-in checks (`Range`, `IsMember`, `CheckedTransformer`, `PositiveNumber`) capture their own data, so the copies are self-contained — this is what lets the entry outlive the option tree. The copy is taken in `registerOne` **after** the spec's `cfg` tuple has been applied, not inside the `ConfigKey` functor: every token-carrying spec adds its checks *after* the token (`:877` then `PositiveNumber` at `:878`, `:914`/`:922` then `Range` at `:915`/`:923`, `:936` then `Members` at `:937`), so a copy taken in the functor would miss them. `Option::validators_` is protected and there is no public `get_validators()`, so the copy walks `get_validator(index)` until `OptionNotFound` — the loop `validateValue` uses today.

*Alternatives:* keep the `CLI::Option*` in the entry and read the validators at validation time — smaller, but it reintroduces the lifetime coupling this change removes and the table stops being a value; or store one merged `std::function` that runs the whole chain — fewer fields, but per-validator error text (the `Invalid value for KEY` messages) would have to be rebuilt by hand.

**D2 — The kind is derived, not declared.**
`registerOne` computes it from `Spec::binding_type`: `bool` → Boolean (flags are booleans, and this test comes first because `std::is_arithmetic_v<bool>` is true), arithmetic types including `std::optional<T>` of them → Number, everything else → String. A small constexpr helper finds the `cfg::ConfigKey` element inside the spec's `cfg` tuple (`if constexpr` over the pack) and yields its name; specs without a token record nothing.
*Alternatives:* keeping the kind on the token (`cfg::ConfigKey{"crf", Kind::Number}`) moves the declaration next to the option but leaves it hand-written and able to drift from the binding — the drift is the defect being removed; keeping `kKeys` as the kind source leaves two lists and only catches drift in a test.

**D3 — Canonical order stays one literal list of names, and assembly enforces the bijection.**
Registration order is an accident of `buildAppTree`'s call sequence — it matches `kKeys` today — so the order is its own contract: `std::to_array<std::string_view>({…})` in `config_store.cpp`. `assembleKeyTable` returns an error naming the offending key for: a token whose key is not in the order, an order entry with no token, and a duplicate entry. The error travels through `CmdParseResult::error`, which `buildAndParse` already returns early on (`cmd.cpp:1026`), so a drifted table fails the run before any command executes instead of silently changing `config list`.
*Alternative:* ordering by CLI11 group + registration index derives the order too, but it pins "the organize subcommand's group comes last" as an unstated consequence of group naming — a file-format change waiting to happen.

**D4 — Assembly is a pure function so the rejections are testable.**
The real registration is always self-consistent, so the three mismatch paths can only be exercised with synthetic drafts; `assembleKeyTable` takes plain spans and touches no app, no `CLI::Option*` and no global. `registerAll` records entries through a `std::vector<KeyDef>&` on `CmdParseResult` — the object every registrar already receives — and `buildAppTree` assembles the table once after the last registration.
*Alternative:* assemble by CLI11 group + registration index (D3's rejected option) and test only the happy path through `testutils::parseArgs` — no synthetic entries, but then no test would exercise the rejection paths at all.
*Accepted cost:* the draft entries buffer lives on `CmdParseResult` between registration and assembly (`keyEntries`: registration order, consumed once by `assembleKeyTable`), so the parse result carries a field only the assembler reads. Hoisting it into `buildAppTree` would thread a new parameter through the five registrar signatures for no behavior gain — the two-pass shape is the decision, not an accident of plumbing.

**D5 — The table lives in the parse result.**
`CmdParseResult` gains the assembled `KeyTable`; `configstore::keys()`/`isKnownKey`/`jsonKindOf` become table operations (`table.find(key)`), `validateValue` becomes `table.validate(key, text)`, and `configKeyRegistry()`/`captureConfigKey` are deleted. `load`'s unknown-key check (`config_store.cpp:211`) is a table read too, so `load` takes the table and `injectConfigDefaults` (`cmd.cpp:971`, called from `buildAppTree` after assembly) passes it through. `save()` walks the table's canonical keys because serializing a value needs its `JsonKind`; its call sites (`config_command.cpp:87,103`) sit under `runConfigCommand`, which already holds the parse result, and the unit cases build a table through `testutils::parseArgs`. `runConfigCommand` already receives the parse result (`config_command.h:11`), so its call sites do not change.
*Cost:* `config_store_tests.cpp:199`'s hand-made-app setup loses `captureConfigKey` and is rewritten against `assembleKeyTable` + `KeyTable::validate`, keeping its named failure mode ("validators run in order, transform canonicalizes").
*Alternative:* an immutable file-static table filled once at registration — less parameter traffic, but keeps a hidden global dependency in the module whose whole point is to stop reaching for globals.

**D6 — Completion's config-key map becomes a projection.**
`cfg::ConfigKey`'s functor drops its `completion::recordConfigKey` call; after assembly, `buildAppTree` walks `table.keys` and records key → long name into the completion registry (`completion_registry.cpp:33-35`). `assembleKeyTable` stays pure — it returns the table, its caller writes the global (D4). The emitter (`completion_emitter.cpp:188`) and the registry accessors are untouched, and the key set can no longer disagree with itself.
*Alternative:* the emitter reads the `KeyTable` directly — cleaner in principle, but it threads the table through the emitter's model builder for no behavior gain, and the emitter's other maps stay registry-side regardless.

**D7 — `validateValue` keeps all three steps.**
List walk (transform-as-validator first), then the kind type check for Boolean/Number text (`config_store.cpp:99-110`), because flags carry no validators and `config set` takes raw text. A key that is in the table always has an entry (D3), so the "known key, no registered option → unvalidated" branch (`:84-86`) disappears rather than being reimplemented, and `table.validate` requires a key present in the table — every caller rejects unknown keys first (`config_command.cpp:65,76,100`), which is why the `:199` case's `no-such-key` assertion goes with the branch.
*Alternative:* drop the kind text check and rely on the option's validators — but `--yes` is a flag with no validators, so `config set yes maybe` would be stored and only fail much later at CLI conversion.

## Risks / Trade-offs

- [Copying the validator list loses canonicalization] → The transform *is* a validator in CLI11 2.7.2 (`CLI/impl/Option_inl.hpp:151-163`), so the copy is the whole contract; the probe on the current binary (`config set color ALWAYS` → `"always"`) plus the re-pointed `config_store_tests.cpp:199` case — which asserts canonicalization both on a synthetic transformer and on the real registered `color` entry — pin it.
- [A future CLI11 upgrade moves transforms out of the validator list] → The change documents the dependency in the table's comment; the canonicalization case fails loudly if that happens.
- [`model-dir`'s position or a key rename is forgotten in the order list] → Startup error naming the key (D3), plus the unit cases for both directions.
- [Tests that build a `CLI::App` by hand lose their setup helper] → `captureConfigKey`'s only non-production caller is `config_store_tests.cpp:199`; it moves to `assembleKeyTable` with the same failure mode it names today.
- [Stricter startup can break a real invocation] → Only for a state that is already a defect (a token/order mismatch cannot exist in a compiling tree with matching lists); the error names the key so the fix is mechanical.
- [Behavioral asymmetry stays: `preset`/`output-format` reject uppercase where `color`/`force-conflict-handling` accept it] → Out of scope by Non-Goals; it is today's behavior and a candidate for its own change (the fix would be giving those options a normalizing transform, not weakening the table).

## Migration Plan

None: internal only, no `config.json` format change, no CLI surface change, no spec-level behavior change. Rollback is a revert of the single refactor commit.
