# Design

## Context

See proposal.md — Why. The current shape that constrains the approach:

- `src/cmd/option_specs.h` — completion data is captured while options are registered: `cfg::Members`/`cfg::CheckedTransformer` call `completion::recordCandidates`, `cfg::Range`/`PositiveNumber` call `recordNumeric`, `cfg::Path` calls `recordPath`, and a positional with `Members` calls `recordPositional` keyed by option pointer.
- `src/cmd/completion_registry.*` — those captures live in process-global maps keyed by canonical long name (`"--crf"`) or, for positionals, by `CLI::Option const*`.
- `src/cmd/completion_emitter.*` — `buildCompletionModel()` walks a freshly built (config-injection-free) app tree, projects the registry into a shell-agnostic model, and renders two scripts whose candidate data is embedded as literal tables.
- `src/cmd/completion_command.cpp` — executes the `completion` subcommand; `commandLineInit` returns early for it, so it never reads stored config values.
- `src/cmd/cmd.cpp` — `-T/--template` is `cfg::Expected{0, 1}` with no candidates; the `template` subcommand registers `action` (a `Members` positional), `name` and `key` positionals with no candidates; `buildAppTree` assembles the template key table after registration, the same place config keys are recorded for completion.
- The template name slots therefore emit empty slots today, and the emitted glue hands an empty positional slot to the shell's native file completion (`complete -o default` in bash).

## Goals / Non-Goals

**Goals:**

- Template names complete from live state in both slots (`-T/--template` value and the `template` name positional), for bash and PowerShell.
- Emission stays a pure function of the build: no user data in the script, byte-identical output for the same build and shell.
- One mechanism, not a template special case: any future live-data slot registers a provider id and gets the same glue.

**Non-Goals:**

- Caching or background refreshes of provider output (one spawn per TAB on the three affected slots, measured ~50-120 ms; revisit only if it hurts).
- Hiding `--candidates` from `encro completion --help` or from the completion scope's own candidate list — this repo has no option-hiding mechanism, and the flag is a real, documented option.
- Additional shells (zsh/fish) and completing template names anywhere a template cannot be named.

## Decisions

### D1: A provider id per slot, resolved by the script at TAB time

A slot can carry a provider id instead of (or beside) its static candidates. The emitted script records the id, and the glue asks the binary for that slot's values when completion actually runs.

Alternatives rejected:

- **Bake names into the script at emission.** Stale by construction, and it makes emission depend on the user's template store, which the emission determinism requirement forbids.
- **Refresh the installed script on `template save`/`delete`.** bash installs through bash-completion's lazy-load directory, which a running shell sources once, so a save would not take effect in the session where the user is working; it also makes a store-writing command write completion files.
- **Have the glue scan the templates directory itself** (via `encro template path` plus globbing). Duplicates the name rules (lowercasing, legality, the reserved schema file) in two shell dialects, where they drift.

### D2: The channel is `encro completion --candidates <slot>`

One flag on the existing `completion` subcommand; slot ids are a small closed namespace whose only member today is `template-name`. Dispatch happens in `runCompletionCommand` before any shell/install handling, so the channel needs no shell argument and stays on the code path that provably never reads stored config values.

Alternatives rejected:

- **`encro template list --names`**: puts a machine format on a display action, and — decisively — `list` loads and validates every template file, so one malformed file would break name completion. The provider must stay a directory scan.
- **A new hidden subcommand**: `buildAppTree` passes a hardcoded `std::array<CLI::App const*, 6>` to the help formatter, and the help-layout contract pins the command list; a seventh entry costs more than the flag it would replace.
- **Serving candidates through the existing positional verb slot** (`encro completion candidates <slot>`): the shell positional is a `Members{bash, powershell}` and would grow a third legal value offered to humans.

The unknown-id error doubles as a version-skew guard: a script from a newer build asking an older binary gets a non-zero exit instead of a silently wrong answer.

The channel answers for the template-name slot with the template files' stems that survive `templatestore::normalizeName` unchanged. That filter is load-bearing, not cosmetic: `templatestore::list` lowercases a stem but never validates it, so a hand-placed `a b.json` would otherwise become a candidate that the glue's word splitting (bash) and `:`-delimited provider table cannot carry.

### D3: Provider ids are one shared constant

`kTemplateNameProvider = "template-name"` lives beside the registry in `src/cmd/completion_registry.h`; both the registration token and the provider dispatch reference it, so the two halves cannot drift into a silent no-op.

### D4: Declarative provider tokens, beside `cfg::Path`

`cfg::Provider{id}` records the provider for its slot in either key space: a named value option (`-T/--template`) is keyed by long name, a positional by option pointer like `recordPositional` records its candidates. The emitter keeps walking the tree generically — it never learns that a `template` subcommand exists.

Alternative rejected: identifying the slots by name inside the emitter. The generator does already hardcode the `config` action verbs in both glues, so a template-specific hardcode would work — but it would turn one per-shell special case into a set of them, and the next live-data slot would have no path that does not add another.

### D5: The key slot is static, recorded where the template table is assembled

`buildAppTree` already assembles `result.templateTable` after registration and records config keys for completion at that point. It records the template keys the same way — `completion::recordPositional(templateSub->get_option_no_throw("key"), sortedKeys)` — which keeps the key slot free of any provider call.

CLI11 v2.7.2 keeps positional options in the same `options_` list as named ones and `get_option_no_throw` scans it with `check_name`, so the declared name `"key"` resolves the pointer without threading it out of `registerTemplateSubcommand`. If a future CLI11 drops that, the registrar can return the pointer instead; the registry API does not change.

### D6: Model and emission shape

- `OptionInfo` gains `provider` (empty = static); positional slots become a struct carrying their candidate list plus the same field. The existing `std::vector<std::vector<std::string>> positionals` already had to change shape, and a parallel array of provider ids would let the two vectors drift in length.
- bash: `_ENCRO_PROV_<id>="<provider>"` for a value option, `_ENCRO_POSPROV_<scope>="<slot0>:<slot1>:<slot2>"` for positionals (empty field = static), and one helper that runs the provider and fills `COMPREPLY`.
- PowerShell: `$__encroProvider = @{ 'template' = 'template-name' }` and `$__encroPosProvider = @{ 'template' = @('', 'template-name', '') }`, consumed by the same two branches that already handle static candidates.

Alternative rejected: a sentinel entry inside the candidate list. It conflates data with markers, and a slot could legitimately want both static and live candidates.

### D7: The glue invokes the binary through the word the user typed

bash uses `${COMP_WORDS[0]}`, PowerShell the command name from the AST, so an `encro` reached by absolute path or a shim still resolves. Both split the output on line breaks (names are `[a-z0-9._-]`, so no quoting or escaping is needed) and treat a non-zero exit or missing binary as "no candidates".

### D8: Provider-backed slots suppress the file fallback

They behave exactly like enumerated slots: no match means no candidates and no file names. `save` is unaffected — a new name is typed, not completed, so suppression only removes noise (`.json` files from the working directory), never an ability.

### D9: `--candidates` is an ordinary registered option

It excludes `--install`/`--uninstall` (the existing mutually-exclusive pattern) so the channel cannot be silently swallowed by an install request. It otherwise behaves like any flag: it shows in `encro completion --help`, and the completion scope offers it as a candidate like every other registered option. The help tests assert substrings, and the usage lines are hand-written, so neither changes.

## Risks / Trade-offs

- **Per-TAB spawn latency (~50-120 ms)** → only the three template-name slots pay it; keys, options, enumerated values and config keys keep their spawn-free paths. If it proves annoying, a shell-side cache of the provider output is an additive change that touches only the glue.
- **Version skew between script and binary** → an unknown slot id exits non-zero and the glue treats it as an empty set, so a new script against an old binary offers nothing rather than something wrong; the script header and install output already tell users to re-run the install after upgrading.
- **Binary not on the shell's PATH at completion time** → the dynamic slots go quiet; every static slot keeps working. Nothing to catch: the glue discards provider errors instead of surfacing them in the prompt.
- **`--candidates` visible in help and in the completion subcommand's own candidates** → accepted noise, documented as the machine channel; adding an option-hiding mechanism for one flag is more code than the noise is worth.
- **Reliance on CLI11 indexing positional options by name (D5)** → verified against the pinned v2.7.2; the fallback (hand the pointer out of the registrar) is local to one call site.
- **Candidates are slot-addressed, not action-aware** → `encro template list <TAB>` offers names and `encro template delete anime <TAB>` offers keys; accepted noise, because teaching both glues a per-action table costs more than the occasional suggestion the parser then rejects.

## Migration Plan

No data migration. An installed script is a generated snapshot, so the new slots appear only after `encro completion <shell> --install` (or a manual re-emit) — the scripts already print that instruction. Rolling back the binary while an updated script stays installed degrades the three template slots to empty candidates: the provider call fails, the glue suppresses the fallback, and no other slot is affected.
