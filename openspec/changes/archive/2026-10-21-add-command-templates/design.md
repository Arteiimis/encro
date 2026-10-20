# Design

## Context

The CLI already has one stored-defaults layer: `injectConfigDefaults` (`src/cmd/cmd.cpp:693-737`) loads `config.json` through a registration-time key table (`KeyDef`/`KeyTable`, `src/cmd/config_store.h:29-50`) and applies each value to the matching option with `default_str(value)` plus `force_callback()`. Explicit command-line values win because CLI11 only uses a default when the option has no results, and the forced callback is what writes the bound field. Every run parses twice (`src/cmd/cmd.cpp:910-922`): a probe parse with no injection, then — for everything except `config`/`completion` — a second parse with injection, so store-reading commands still work when the store is unreadable.

Option registration is declarative (`src/cmd/option_specs.h`): an `OptSpec` is a name, a bound field, a description and a variadic list of `cfg::` tokens. `cfg::ConfigKey{"crf"}` is what makes an option a config key, and `registerOne` records a `KeyDef` (kind from the binding type, long name, built-in default, a copy of the validator chain) as a side effect. `configstore::assembleKeyTable` then rejects any drift from `canonicalKeyOrder()` (`src/cmd/config_store.cpp`), a deliberate file-format contract.

Two CLI11 mechanics shape every option here, verified in the pinned 2.7.2 source: a scalar option's default `MultiOptionPolicy` is `Throw` (`CLI/Option.hpp:99`), so a second occurrence of the same option is an error rather than "last wins" (`CLI/impl/Option_inl.hpp:694-715`); and cross-option exclusion/requirement checks fire only for options with results — `opt->count() > 0` (`CLI/impl/App_inl.hpp:1566-1570`), which a forced default never changes.

## Goals / Non-Goals

**Goals:**

- One stored-defaults pipeline for both stores: the template layer reuses `configstore`'s load/save/validate rather than introducing a second file format implementation.
- Template values behave as defaults, so a template can never make a run unstartable through the CLI's exclusion rules, and any single option can be overridden or cleared.
- The templateable set, the generated schema and the negation-form completeness are one compile-time declaration, so they cannot disagree.

**Non-Goals:**

- No argv rewriting, no `take_last()` change, no CLI11 native config-file layer (see D2).
- No path pinning, no template inheritance, no save-on-run, no `organize`-specific keys (proposal non-goals).
- No committed schema snapshot and no schema distribution channel beyond the binary.

## Decisions

### D1 Template store: a second `configstore` key table, one file per template

`cfg::TemplateKey{"type"}` marks an option as templateable; `registerOne` records a `KeyDef` for it exactly as `ConfigKey` does, into a separate `entries` vector. A new `configstore::canonicalTemplateKeyOrder()` is assembled with the existing `assembleKeyTable`, producing a `KeyTable` for templates. The templates directory is `<config file's parent>/templates`, so `ENCRO_CONFIG` relocates templates with the config file and no new environment variable appears. Each template is `<name>.json`; reading uses `configstore::load` (missing = empty, malformed = error, unknown keys reported), while writing goes through a template-specific writer rather than `configstore::save`: that function can only emit keys present in the table, so it can neither put the reserved `$schema` key first nor preserve a hand-edited key outside the set, and the spec requires both. The writer orders known keys by the table, writes `$schema` first, and passes through unknown keys unchanged. Name validation (`[a-z0-9._-]`, first char alphanumeric, case-insensitive lookup) lives beside path resolution, and the reserved name `templates.schema.json` is excluded from listing and lookup.

*Alternatives:* a single `templates.json` map — every `save` becomes read-modify-write of all templates, one bad entry breaks all of them, and `list` needs a parse; CLI11's own config format — see D2.

*Why:* a template file that is literally a `config.json` in another directory means users already know how to edit it, `boost::json` is already a dependency (`src/cmd/config_store.cpp:7`), and Windows paths need no quoting dialect because JSON escapes them.

### D2 Application: forced defaults, layered by merge order

`injectConfigDefaults` becomes a two-source injection: load config values, load the selected template's values, let the template overwrite same-named keys in one `std::map`, then run the existing injection loop once. Later `default_str` writes win, so folding the precedence into the map keeps it in one place and leaves the twin-option loops untouched.

*Alternatives considered and rejected:*

- **CLI11 native config-file layer** (`App::set_config`, `CLI/impl/App_inl.hpp:458-483`): gives precedence, validation and `to_config(Active)` capture for free, but registers a real `--config` option that would appear in help and completion (both walk the option tree: `src/cmd/help_layout.cpp:284`, `src/cmd/completion_emitter.cpp:47`); silently ignores a missing file (`_process_config_file(..., throw_error=false)`, `:1374-1393`); and turns template values into *results*, so a stored `dry-run` would make an explicit `--crf` an `ExcludesError`. Its unique advantage — vector-valued options and positional inputs — is precisely what the spec excludes.
- **argv token templates** (prepend or merge): a prepended duplicate scalar meets `MultiOptionPolicy::Throw`, so it needs `take_last()` on every option (changing duplicate-argument errors) or an argv merge that reimplements short-flag bundling, `--opt=value` and negation parsing.

*Consequence to accept:* because template values are defaults, they also skip `needs`-style checks — a template storing `image-quality` without `compress` is silently inert, exactly as `config set image-quality` is today. That is the established semantics of the layer, not a new hole.

### D3 Templateability is declared, not derived

Only options carrying `cfg::TemplateKey` are captured, applied and exported to the schema; `-i`, `-I`, positional inputs, `--state-file` and `-v` simply carry no token. A capture that names an option with results but no token is an error, which makes "why didn't `--input` get saved" a message instead of a mystery.

*Alternatives:* a runtime deny-list — invisible in the code that declares the option, and it cannot answer the negation-form question.

### D4 Capture from the probe parse

`template` actions dispatch on the probe result, like `config`, so `save` never sees config-injected values. The template subcommand enables CLI11 `fallthrough` (only on itself, not on the root app) so `encro template save anime --crf 24` resolves the parent's options after the subcommand name. Capture walks the registered options, takes every templateable one with `count() > 0`, and stores `Option::results()` as text; values were already validated by the parse that produced them.

*Why:* no second parser, no duplication of the option table, and the values a template stores are exactly the ones the CLI accepted.

### D5 Schema generated from the registration table

`KeyDef` gains `desc`, `enumValues` and `minimum`/`maximum`, filled while the option is registered: the description comes from the `OptSpec`; members come from `cfg::Members`; bounds come from `cfg::Range`/`cfg::FloatRange`, and the stateless validators `cfg::PositiveNumber`/`cfg::PositiveFloat` contribute `minimum: 1` and `exclusiveMinimum: 0` respectively (they carry no member values of their own, so the extraction maps the token type to the bound it enforces — this is what gives `jobs` a `minimum`). A `templateSchema(KeyTable const&)` renders JSON Schema from that table: type from `kind`, `enum` from `enumValues`, `minimum`/`maximum` from the bounds, `default` from `builtinDefault`, `description` from `desc`, `additionalProperties: false`, and `$schema` declared as a reserved string key. `save` writes `templates.schema.json` beside the templates and `encro template schema` prints the same bytes.

*Alternatives:* a hand-written schema plus a consistency test — two sources of truth for the key set; a committed generated snapshot — rejected by the user, and it would need a build step and a drift test.

*Consequence:* the loader must skip `$schema` before the unknown-key check in `configstore::load` use, so template loading filters it out of `LoadResult::unknownKeys` and out of the value map.

### D6 Diagnostics follow the config store

An unknown key in a template warns and is ignored (the config store's `warnUnknownKeys` shape, extended to name the template) while still being preserved by the next rewrite, malformed JSON names the file and fails, and an unknown template name fails with a pointer to `encro template list` rather than a bare ENOENT. The reserved file name `templates.schema.json` is skipped by listing and by name lookup, so the schema can never be selected as a template. `--template` combined with `config`, `completion` or `template` is an error: those paths never read stored defaults, and a silently ignored flag is worse than a rejection.

### D7 Nine negation forms, no layout work

`--quiet`, `--log-json`, `--debug`, `--full-progress`, `--resume`, `--restart`, `--video-webp`, `--dry-run` and `--pack-only` gain `--no-...{false}` twins. Help already collapses a registered pair into `--[no-]name` (`src/cmd/help_layout.cpp:76-84`) and the completion emitter already emits negation variants, so this is a token per option plus the `docs/public-api.md` table. `--no-debug` sets `debug` false; the verbosity derivation only raises (`std::max(verbosity, 2)` when `debug`), so `-v --no-debug` keeps verbosity 1.

### D8 Verbose attribution in the prelude

After injection the run knows which template was applied; with verbosity raised, print one line naming the template and its path where the prelude already narrates effective settings.

## Risks / Trade-offs

- [Template values skip `needs`/`excludes` checks] → Accepted: this is the config layer's established semantics; the spec states it, and a template that stores a dependent option without its prerequisite is inert rather than fatal.
- [A missing templates directory on a read-only profile fails `save` after validation] → `save` reports the write error naming the path and leaves no partial template (write happens after the merge is built in memory).
- [Threading the selected template name into the second parse touches the hot path] → The parse signature gains one string; a test asserts a plain run with no `--template` is unaffected (no store read, no output line).
- [Schema and key table could drift if a future option is marked `ConfigKey` but not `TemplateKey` by accident] → The schema is generated from the same table the feature uses, so drift is impossible in the shipped direction; a completeness test asserts every templateable key with a negation-capable boolean kind appears with a `--no-` twin.
- [Windows path values round-tripping] → JSON escaping handles them; a test stores `-o E:\out with space` and asserts the resolved output directory.
- [`fallthrough` routing is documented in CLI11 but unused in `src/` today] → The first `save` test uses the post-subcommand order (`encro template save anime --crf 24`), so a routing surprise fails immediately rather than after the injection work; the fallback is documenting the pre-subcommand order instead.
- [D7 changes the CLI surface, so the public API versioning rule applies] → MINOR bump by the documented rule; `docs/public-api.md` gains the negation column entries and the templates section in the same commit.

## Migration Plan

No stored data exists to migrate; a first `save` creates the templates directory and the schema. Rollback is deleting the subcommand and the flag — template files left on disk become inert because nothing reads them.
