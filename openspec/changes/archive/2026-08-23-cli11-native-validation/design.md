# Design — cli11-native-validation

## Context

Current state (see proposal.md for motivation): `src/cmd/cmd.cpp` registers all options data-driven from four `CmdFlagDef` arrays, then copies parsed values into `CmdParseResult` via a parallel hand-written `applyMap` (`ResultSetter` lambdas keyed by the same name strings as the def arrays). Validation lives partly at parse time (CLI11 `excludes()`, `ParseError` catch) and partly in `src/cmd/config_builder.cpp` (enum/range/alias checks with hand-written messages). The `preview` subcommand has its help flag disabled (`set_help_flag("")`), hand-written error checks, a hard-coded help text (`buildPreviewHelpText`), and unbound options. CLI11 v2.7.2 is in use (unpinned `add_requires("cli11")`; API facts below were verified against the v2.6.2 headers and hold identically in v2.7.2, per line-by-line comparison).

Verified CLI11 facts the design relies on (headers v2.6.2, identical in v2.7.2):
- `Option::excludes()` exists in pointer and string form (`excludes("--name")`); native message is `ExcludesError: "<name> excludes <other>"` (`Error.hpp:304`, names via `get_name()`).
- `RequiredError(name)` → `"<name> is required"` (`Error.hpp:230`).
- `CallForHelp` is a `ParseError` subclass (`Error.hpp:174`, exit code Success) — must be caught *before* `ParseError`. It is thrown from `_process_help_flags` (`App_inl.hpp:1320`), **before** required-checks run, so `encro preview -h` without `original` still shows help. `Error` has **no `get_app()` accessor** — the caller must pick the app (see Decision 4).
- Subcommands inherit the parent's help flag (guarded by non-null, `App_inl.hpp:34-35`) and formatter (unguarded copy, `App_inl.hpp:58`) **at construction time** — registration order matters (see Decision 4).
- `IsMember`/`CheckedTransformer`/`Range`/`PositiveNumber`/`NonNegativeNumber` are available in `<CLI/CLI.hpp>`.
- Under `expected(0,1)` with a non-empty `default_str`, a bare flag (no value) is filled with the default string (`App_inl.hpp:2279-2285` → `Option_inl.hpp:435-441`) and validates/assigns as if the default were passed — the bound variable ends up at its default. (Empty `default_str` would yield an empty result instead; all options here have non-empty defaults.)
- `get_type_name()` = bound type name (`TEXT` for string, `INT`/`NUMBER` for int/size_t, `Option_inl.hpp:538-549`) plus each validator's description (`int in [2 - 31]`, `{mp4,webp}`, …). Today nothing is bound and no validator is attached, so it is empty and the formatter's `TEXT`/`text` suppression matches nothing — dormant defense (see Risks).
- `IsMember` with a filter rewrites the input to the canonical member (`Y` → `y`); `CheckedTransformer` rewrites input keys to their mapped value (`vid` → `video`) — this is what makes the `tolower`/alias code in `buildConfig` safely removable (Decision 5).

## Goals / Non-Goals

**Goals:**
- All value validation (enums, aliases, ranges), option conflicts, and dependencies run at parse time with native messages.
- `CmdParseResult` populated by direct option→field binding; no post-parse copy map.
- `preview` subcommand uses native help/validation with a small dedicated formatter.
- Byte-stable `-h`/`-hh` two-tier help (contract in `cli-help-tiering` spec).

**Non-Goals:**
- No CLI11 version bump.
- No behavior change to: output-alias resolution (`+`, `=`, `input://`, `common://`), filesystem validation, output-layout (`--keep`/`--force-conflict-handling` *semantics*), `--video-codec` (open set of ffmpeg encoder names — must NOT be locked to a list).
- Cross-option *value-conditional* checks stay in `buildConfig` (CLI11 `needs()` cannot express "value of X == Y").
- `--version` stays a hand-registered flag (unchanged), main `-h`/`-hh` counting stays custom (CLI11 has no tier concept).

## Decisions

### Decision 1: Replace def arrays + applyMap with hand-written bound registration

Delete `CmdFlagDef`, the four flag arrays, `registerCmdFlag`, `buildXxxApplyMap`×4, `buildResultApplyMap`, `optRegistry`, `PendingExclusion`, and the generic `parseAndPopulate` copy loop. Register all 30 options by hand (29 flag defs + the positional option), each binding directly to a `CmdParseResult` field:

```cpp
// before (machinery) vs after (direct)
auto* q = processing->add_option("-q,--image-quality", r.imageQuality, "JPEG compression quality (2-31, lower=better)")
  ->expected(1)->default_str("2")->check(CLI::Range(2, 31));
```

Rationale: the `CmdFlagDef.kind` enum is a compressed encoding of "field type"; binding lets the C++ type system express it. The def arrays' only irreplaceable payload is the *advanced* tier list, which survives as a standalone `constexpr std::array<std::string_view>` (`kAdvancedLongNames`) feeding the existing formatter (`collectAdvancedLongNames` machinery deleted). Both intermediate states (defs without applyMap, applyMap without defs) force duplicated keys or type erasure — all-or-nothing wins.

Mapping rules per option:
- `defaultValue == ""` (no default): bind `std::optional<T>` field (or `expected(1)`), `default_str("<display>")` where present (`-q` → `"2"`, `--crf` → `"28"`, `--preset` → `"auto"`).
- `defaultValue != ""` (has default): bind plain field whose initial value is the default (`color="auto"`, `outputFormat="mp4"`, …), `expected(0,1)`, `default_str("<default>")` for the `(=value)` display.
- Booleans: `add_flag` binding the bool field directly.
- `-I/--inputs` and the positional `input-paths` option: bind `optional<vector<string>>`, `expected(0, 1000000)`.
- **Registration order per group must reproduce the current def-array order** — the help formatter renders options in registration order (byte-stability contract).
- `-h,--help` and `--version` stay hand-registered flags on the app (tier counting via `count()`).

Side-effect check (verified against the headers): on any `expected(0,1)` option — `--color`, `-f`, `-t`, `-j`, `--min-vmaf`, `--force-conflict-handling` — a bare flag is already filled with the default string today and silently behaves as the default; the hand-written `results().empty()` guards in the apply map are unreachable dead code (same verdict for the `-q`/`--crf`/`--preset`/`--video-codec` guards on `expected(1)` options). Binding preserves this exactly: **no observable change, no spec scenario needed**.

### Decision 2: Validators per option (parse-time)

| Option | Validator | Removed hand-written check |
|---|---|---|
| `-t,--type` | `CLI::CheckedTransformer{{"vid","video"},{"pic","picture"}}` | `readProcessType` alias+whitelist |
| `-f,--output-format` | `CLI::IsMember{"mp4","webp"}` | `readOutputFormat` |
| `--force-conflict-handling` | `CLI::IsMember({"y","n"}, CLI::ignore_case)` | `readForceNameConflictHandling` (+tolower) |
| `--color` | `CLI::IsMember({"auto","always","never"}, CLI::ignore_case)` | (was never checked at parse time; `prelude.cpp` error branch becomes unreachable) |
| `--preset` | `CLI::IsMember({"auto","p1".."p7"})` | none (was unvalidated) |
| `-q` | `CLI::Range(2,31)` | `applyMediaOptionValidations` |
| `--crf` | `CLI::Range(0,51)` | same |
| `--min-vmaf` | `CLI::Range(0,100)` | same |
| `-j` | `CLI::PositiveNumber` | `readMaxParallelJobs` |
| preview `--start`/`--duration` | `CLI::NonNegativeNumber` | none (was unvalidated) |

`CheckedTransformer` accepts both the mapped keys (`vid`/`pic`) and the canonical values themselves (verified in v2.6.2 `func_`: direct value match is accepted), so `-t picture` keeps working. No explicit `video`→`video` mapping pair needed.

`--video-codec` deliberately gets **no** validator (open encoder set; see Non-Goals).

### Decision 3: Conflicts & dependencies via native declarations

- `excludes(pointer)` where both options are registered in the same function scope (all existing pairs are); removes the deferred `PendingExclusion` mechanism and the dead `excludesDesc` field. Native message `"X excludes Y"` per verified v2.6.2/v2.7.2 behavior.
- Positional `input-paths` ↔ `-i`/`-I`: add `excludes()` between the positional option and both input options (replaces the hand-written branch in `applyInputSelection`).
- `--dry-run` ↔ `--crf`: move from `buildConfig` to `excludes()`.
- `--image-quality` → `needs(--compress)`: existence dependency, expressible natively (replaces the hand-written message in `applyMediaOptionValidations`).
- **Stays in `buildConfig`** (value-conditional, not expressible): `--compress` only with `--type picture`; multi-input only for `video`; multi-input incompatible with `--pack-only`; missing-input summary message (needs positional/`-i`/`-I` tri-state knowledge).

### Decision 4: Native preview subcommand + dedicated formatter

`registerPreviewSubcommand` becomes: bind all fields directly (`r.previewOriginal` etc. — the `PreviewSubcommand` struct shrinks to just `CLI::App*`), `original->required()`, `--start`/`--duration` with `CLI::NonNegativeNumber`, `-h,--help` via `set_help_flag("-h,--help", …)` (call it explicitly on the subcommand — do NOT rely on construction-time inheritance, since the parent cleared its help flag before subcommand creation, verified `App_inl.hpp:34`).

`parseAndPopulate` splits the catch:

```cpp
} catch (CLI::CallForHelp const&) {
  result.help = true;                       // exit code 0 path
  // Error has no app accessor; CallForHelp can only originate from the
  // preview subcommand (the parent app has no native help flag)
  result.helpText = app.got_subcommand(previewSub.app)
    ? previewSub.app->help()
    : app.help();
} catch (CLI::ParseError const& ex) {
  result.error = ex.what();
  result.helpText = app.help();
}
```

Help rendering (chosen option A): a ~30-line preview-specific `formatter_fn` on the subcommand that reuses `formatOptionName`/`formatOptionHelp`/`wrapDescription`/`wrapDescriptionLine`/`resolveHelpTextLayout` and iterates `sub->get_options()`. Delete `buildPreviewHelpText` and `populatePreviewCommandResult` (its required-check role is replaced; remaining field reads happen via binding). **Never** pass the parent's `makeHelpFormatter` to the subcommand — it captures the parent group pointers and would render the whole main option table. Because the subcommand is constructed before `app.formatter_fn(...)` (verified `App_inl.hpp:58`), it does not inherit the parent's lambda formatter anyway; setting its own `formatter_fn` explicitly is required.

Native `RequiredError("original is required")` replaces the old message that also carried the `<original> [<encoded>]` usage hint; keep the hint by wording the `original`/`encoded` option descriptions so preview help still documents both positionals.

Error/help paths never complete the parse, so `result.preview` is NOT set there — the two existing `cmd_cmd_tests` cases asserting `result.preview` on the old hand-written messages must be updated (task 5.1).

### Decision 5: buildConfig slims down, keeps its interface

`buildConfig(CmdParseResult const&) -> eh::Result<AppConfig>` signature unchanged (1152 lines of tests construct `CmdParseResult` directly). Deletions: `readProcessType` (canonical value already in result — just store it), `readOutputFormat`, `readMaxParallelJobs`, `readForceNameConflictHandling`, the range checks in `applyMediaOptionValidations`, the `--dry-run`/`--crf` and `--image-quality`-requires-`--compress` checks (`.error` paths gone). `readProcessType`'s alias mapping is already applied by the transformer at parse time. Keep: fs checks, output-alias resolution, value-conditional checks (Decision 3), `nvencPreset == "auto"` reset.

### Decision 6: Test strategy

- `tests/cmd_cmd_tests.cpp`: add parse-time failure cases (invalid enum/range/conflict per spec scenarios); assert results via `CmdParseResult.error` text now matching native style — **pin the actual native texts from the first test run**, then freeze them in assertions.
- `tests/cmd_config_builder_tests.cpp`: delete test cases for checks that moved to parse time (invalid process type, invalid format, jobs=0, out-of-range `-q`/`--crf`/`--min-vmaf`, force-conflict-handling value, image-quality-without-compress, dry-run+crf, positional-vs-`-i`/`-I`); keep alias-mapping tests but move them to `cmd_cmd_tests` (transformer runs at parse time); keep value-conditional and fs tests unchanged.
- `tests/cmd_help_tiering_tests.cpp`: must stay green with zero edits (byte-stability regression gate).
- E2E: check `tests/e2e/` for message-text assertions; update if any.

## Risks / Trade-offs

- [New error-message texts (all validation paths)] → Accept explicitly (user-approved for native style); tests are updated from the first actual run output, not guessed.
- [Binding + validators populate `get_type_name()` (type + validator descriptions, e.g. `INT:int in [2 - 31]`, `{mp4,webp}`), which today is empty — the formatter's `TEXT`/`text` suppression matches nothing] → Extend/repair the type-column suppression so **no** type names render in `-h`/`-hh` (byte-stability); an automated full-text snapshot diff of `-hh` output is added (task 5.5) because the tiering tests are substring-based and cannot catch column drift.
- [Registration-order drift silently reorders help] → Hand-written registration mirrors def-array order one-to-one; help-tiering tests + `-hh` snapshot diff during review.
- [Bare flag on `expected(0,1)` options silently acts as default] → Verified NOT a behavior change (CLI11 fills the default string today too); no mitigation needed, noted in Decision 1.
- [`original->required()` message is `"original is required"` — loses the `[<encoded>]` usage hint from the old message] → Carry the hint in the `original`/`encoded` option descriptions (task 3.1); preview help stays informative. Not spec-relevant ("clear missing-arguments error").

## Migration Plan

Single-landing refactor; no data migration. Rollback = `git revert` of the implementation commit (tests + code land together per project convention). CLI11 dependency unchanged.

## Open Questions

None — all decisions resolved during exploration (option A help renderer, native messages, binding approach, def-array removal). Remaining detail choices (exact native message texts, preview help wording) are answers-findable-at-implementation-time, not scope-changing.