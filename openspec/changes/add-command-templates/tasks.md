# Tasks

## 1. Registration contract

- [x] 1.1 Add a `cfg::TemplateKey` token beside `cfg::ConfigKey`, a `canonicalTemplateKeyOrder()` list, and assembly of a second `configstore::KeyTable` from the tokens recorded during registration; verify with a unit test that the assembled template table contains exactly the marked keys, rejects an out-of-order token, and rejects a key missing from the order list.
- [x] 1.2 Capture schema metadata while an option is registered — description from the `OptSpec`, enumerated values from `cfg::Members`, bounds from `cfg::Range`/`cfg::FloatRange` plus the fixed bound each stateless positive validator enforces — into the new `KeyDef` fields; verify with a test asserting `crf` reports 0–51 and default 28, `preset` reports its eight members, `jobs` reports a minimum of 1, and a plain string key reports no bounds.
- [x] 1.3 Mark every templateable option with `cfg::TemplateKey` (all value options and boolean flags on the main command plus the subcommand-shared encode options; leave `-i`, `-I`, positional inputs, `--state-file` and `-v` unmarked), and add the nine negation forms; verify with a test that walks the registered tree and asserts every templateable boolean option exposes a `--no-` twin while the excluded options expose none.
- [x] 1.4 Assert the new negation forms render collapsed in help: `--quiet`, `--log-json`, `--debug`, `--full-progress`, `--resume`, `--restart`, `--video-webp`, `--dry-run`, `--pack-only` show `--[no-]name` in `-hh`; verify with the existing help-tier tests extended to those lines.

## 2. Template store

- [x] 2.1 Implement templates-directory resolution (beside the resolved config file, so `ENCRO_CONFIG` relocates it), name validation (lowercase `[a-z0-9._-]`, letter-or-digit first character), case-insensitive lookup, and exclusion of the reserved `templates.schema.json` name from listing and lookup; verify with tests for a valid name, an uppercase lookup resolving to the lowercase file, `../evil` rejected, a missing directory treated as an empty set, and a directory holding both `anime.json` and `templates.schema.json` listing only `anime` while `-T templates.schema` fails as an unknown name.
- [x] 2.2 Implement template load on top of `configstore::load` with the template table, treating `$schema` as reserved (ignored by the loader, excluded from the value map and from unknown-key warnings), plus the template writer that orders known keys by the table, writes `$schema` first, preserves unknown keys and canonicalizes a hand-compacted file; verify with tests covering a saved template's pretty JSON and native types, `$schema` round-trip, an unknown key surviving a later `save` rewrite while being reported, and canonical rewrite.

## 3. Template schema

- [x] 3.1 Implement `templateSchema(KeyTable const&)` rendering JSON Schema — `type` from the key kind, `enum`, `minimum`/`maximum`, `default`, `description`, `additionalProperties: false` and the reserved `$schema` property; verify with a unit test asserting the rendered document for a boolean, an enumerated and a bounded numeric key.
- [x] 3.2 Write `templates.schema.json` beside the templates on every `save`, and add the `template schema` action that prints the same bytes; verify with a test that saves into an empty directory, deletes a key from the written schema by hand, saves again and asserts the schema is complete again, and a test asserting the printed output equals the written file.

## 4. Template subcommand actions

- [x] 4.1 Register the `template` subcommand with its seven actions (`list`, `show`, `save`, `unset`, `delete`, `path`, `schema`), positional arity validation and CLI11 `fallthrough` on the subcommand only, dispatch it on the probe parse, and add it to the help formatter's command array; verify with tests for bare `encro template` printing help with exit 0, an unknown action failing natively, and a wrong argument count for `show`/`save` failing natively.
- [x] 4.2 Implement `save` capture (templateable options with `count() > 0`, values from `Option::results()`), merge into an existing template or create it, reject a non-templateable option or an empty capture without writing, and print one confirmation line; verify with tests for create, merge preserving other keys, `-i` rejected with the file unchanged, empty capture rejected, and one confirmation line naming template and path.
- [x] 4.3 Implement `list`, `show`, `path`, `unset` and `delete`; verify with tests for the list line per template including its stored options, `show` printing canonical content, `unset` falling back to a stored config value, `unset` of an absent key leaving the file untouched with exit 0, `unset` of an unknown key failing, `delete` removing the file and an unknown name failing.

## 5. Selection, injection and diagnostics

- [x] 5.1 Register `-T`/`--template` as an optional-value option, thread the selected name from the probe parse into the second parse, and print the template list when the flag carries no value; verify with tests for a plain run being unaffected when the flag is absent, `--template` alone listing templates, a repeated `-T` failing, and `--template` combined with `config`, `completion` or `template` failing with an error.
- [x] 5.2 Fold template values into the existing default injection as a merge over the config values and inject once; verify with tests asserting a template value beats a stored config value, an explicit CLI value beats both, an option the template omits keeps the config value, a template value never triggers an exclusion error (`dry-run` with an explicit `--crf`), and `-o +/webp` stored in a template resolves against the run's own input root.
- [x] 5.3 Add the diagnostics: unknown template name pointing at `encro template list`, malformed template naming the file and failing before processing, unknown key warning naming the template while the rest applies; verify with one test per case.
- [x] 5.4 Print the one-line template attribution when verbosity is raised and nothing otherwise; verify with tests asserting the line names the template and path under `-v` and is absent without verbosity flags.

## 6. Documentation and integration

- [x] 6.1 Update `docs/public-api.md`: add the `templates/<name>.json` section (location, shape, `$schema`, unknown-key and malformed-file behavior, schema file), and add the negation forms to the flag tables; verify by reading the rendered tables against `encro -hh` output for the same build.
- [x] 6.2 Update `README.md` with the per-task-template subsection beside the config one, stating the precedence line (command line, then template, then config file, then built-in default) and the two command forms a user types; verify the documented commands run as written against a `TempDir`-backed config path.
- [x] 6.3 Extend the completion-emitter and subcommand-help tests for the new subcommand: `encro completion bash` and `powershell` offer `template` at the main position, and `encro template -h` carries no ANSI styling under the disabled color modes; verify by running those tests plus the existing shell-completion and help-color suites.
- [x] 6.4 Run the full `xmake test-report` (unit and e2e) and `build/windows/x64/release/tests.exe -r console -s` and confirm zero failures, then `xmake fmt`.

## Planning review findings

Reviewed against the artifacts before implementation; every finding below was fixed in the artifacts, and an independent verification pass confirmed all nine resolved with none regressed.

- [P1] Seven-action count (`schema`) contradicted by the "six actions" wording in the action requirement, the proposal and task 4.1 — resolved: `command-templates/spec.md`, `proposal.md` and `tasks.md` now say seven and name `schema`.
- [P1] The generated `templates.schema.json` satisfied the template-name grammar, so it could be listed and selected as a template — resolved: the name is reserved in `command-templates/spec.md` (requirement plus a scenario) and excluded from listing/lookup in task 2.1.
- [P1] "minus the options that carry input and output locations" contradicted the scenario that stores `output` — resolved: the requirement now excludes input locations, `--state-file` and `-v`, and states `-o/--output` is templateable.
- [P2] The `--template` plus `template`-subcommand error existed only in the design — resolved: added to the selection requirement and to task 5.1.
- [P2] The shell-completion candidate scenario and the `encro template -h` color scenario had no task — resolved: added task 6.3.
- [P1] The proposal's Impact omitted `src/cmd/option_specs.h` and `src/cmd/help_layout.{h,cpp}` — resolved: both listed.
- [P2] The design claimed `cfg::PositiveNumber`/`cfg::PositiveFloat` carry bounds; they are stateless — resolved: the design states they contribute `minimum: 1` and `exclusiveMinimum: 0`, and task 1.2 asserts `jobs` reports a minimum of 1.
- [P2] `configstore::save` cannot emit the reserved `$schema` key or preserve a hand-edited unknown key — resolved: the design now specifies a template writer over the table, and tasks 2.2 covers `$schema`-first ordering and unknown-key preservation.
- [P2] `KeyTable` citation (`config_store.h:22-40`) did not span `KeyTable` — resolved: corrected to `config_store.h:29-50`.
- not checked → promoted to a design risk: CLI11 `fallthrough` routing is documented but unused in `src/` today, so task 4.2's first test uses the post-subcommand order.

## Workflow follow-up

- Run the `openspec-verify-change` pass for this change and resolve its CRITICALs before reviewing.
- Run the `code-review` skill with this change's spec path after implementation is committed.
