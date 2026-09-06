## Context

The completion generator (`src/cmd/completion_emitter.cpp`) already walks the live CLI11 app tree for subcommands, options, and exclusions, and merges value metadata from `src/cmd/completion_registry.cpp` — a set of global registries filled declaratively at option registration time by cfg tokens in `src/cmd/option_specs.h` (`cfg::Members` → `recordCandidates`, `cfg::Range` → `recordNumeric`, `cfg::ConfigKey` → `recordConfigKey`). Two gaps break the "declared ⇒ completed" chain:

1. `buildCompletionModel()` hardcodes `model.pathIds = {"input", "inputs", "output", "state_file", "ffmpeg_path"}`. A value option outside this list with no enum candidates reaches `_encro_no_candidates` in the glue, which also suppresses the bash `-o default` file fallback — so `organize --model-dir <TAB>` offers nothing.
2. Positionals have no long name, so `cfg::captureLongName` returns nullopt and `cfg::Members` drops the legal values; `buildScope` skips positionals entirely (`namesOf` has no entry). The glue has no branch for subcommand positional slots, so `encro completion <TAB>` falls through to filename completion.

## Goals / Non-Goals

- Goals: path delegation and positional enum candidates both become declaration-driven; behavior for the five existing path options (their ids remaining in the delegated set) and the fall-through for path positionals are unchanged — the emitted `pathIds` order may change to the registry's sorted order, which no consumer depends on; both bash and PowerShell scripts gain the positional-slot branch.
- Non-Goals: no parser or CLI surface changes; no runtime (script-side) completion protocol — the scripts stay deterministic static snapshots; main-scope positionals (positional input mode) are untouched.

## Decisions

- **D1 — `cfg::Path{}` token, long-name keyed registry.** Mirrors `cfg::Members`/`recordNumeric` exactly: token calls `completion::recordPath(longName)`, `buildCompletionModel` builds `pathIds` from the registry. Alternative rejected: inferring paths from CLI11 validators (no reliable introspection) or from option names (stringly heuristic).
- **D2 — positionals registered by option pointer, not by name.** `cfg::Members::operator()` already receives the `CLI::Option*`; for positionals (no lnames and no snames) it calls `completion::recordPositional(option, legal)`. The emitter resolves these while walking the tree (`buildScope` knows the app and can order positionals), so no scope-name plumbing through the token. Precedent: the config-command registry already captures option pointers (cmd.cpp `buildAppTree` comment, design D3 of that change). Alternative rejected: passing `CLI::App*` into every cfg token (signature churn across all tokens for one consumer).
- **D3 — positional ordering from CLI11, emitted per scope.** `ScopeInfo` gains `positionals`: ordered per-scope entries, each with optional candidates. Bash emits `_ENCRO_POSCANDS_<scope>`; PowerShell emits `$__encroPosCands[<scope>]`. Glue rule (identical in both shells): walking the typed words after the subcommand name, skip flags and words that directly follow (and are the value of) a value-taking option name — the existing `_ENCRO_NAME_ID`/`_ENCRO_VALUE_IDS` tables already identify those, and both glue loops already iterate the words for scope detection; the count of remaining words selects the positional slot; slot with candidates → offer filtered by prefix (no match ⇒ suppress file fallback, same as enum options); slot without candidates → fall through to shell-native completion; index beyond the last positional → no candidates. The positional branch sits AFTER the existing config `--set` value branch (config has zero positionals; a past-the-end result there would break config-key value completion).
- **D4 — `Path{}` does not apply to positionals.** Path positionals need no metadata: fall-through already yields file completion, and preview/organize prove it. Only the enum case needs data. Keeps the token single-purpose. Preview's `--output` is annotated alongside the main IO `--output`, so its delegation no longer depends on the accidental shared normalized id.

## Risks / Trade-offs

- [Glue grows one more branch in two shells] → The branch reuses the existing scope loop (bash) / typed-word walk (PS) to skip flags and option values; both already iterate the words for scope detection. Real-shell TAB probes in `tests/cmd_completion_smoke_tests.cpp` (opt-in via `ENCRO_TEST_COMPLETION=1`) cover each branch per shell.
- [Pointer-keyed registry depends on app-tree lifetime] → Same lifetime contract as the existing config-store capture: the app is leaked deliberately in `buildAppTree`; completion models are built from that same tree.
- [Behavior change for `--model-dir` slot] → Previously nothing, now directory completion; strictly additive, called out in the delta spec scenario.

## Migration Plan

Single build; no persisted state touched. Users regenerate scripts (`encro completion <shell> --install`) to pick up the new tables, same as after any upgrade.
