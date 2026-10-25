# Tasks

## 1. Provider plumbing

- [x] 1.1 Add `kTemplateNameProvider` plus provider recording and lookup to `src/cmd/completion_registry.{h,cpp}` — a provider id on `ValueInfo` for named value options and one on the pointer-keyed `PositionalInfo` record, mirroring the existing candidate/numeric/path captures (`recordProvider`, `recordPositionalProvider`, `positionalInfoOf`); verify in `tests/cmd_completion_registry_tests.cpp` that both key spaces record and re-record, and extend `RegistryGuard` so the new map is restored after each case (run `xmake test-report --tag="[completion]"`).
- [x] 1.2 Add the `cfg::Provider` token to `src/cmd/option_specs.h` beside `cfg::Path`/`cfg::Members`; verify in the same registry test that an option registered through it lands in either key space and that a positional keeps its optional static candidates alongside a provider.
- [x] 1.3 Project providers into the model in `src/cmd/completion_emitter.{h,cpp}`: `OptionInfo::provider` for value options and a positional slot struct carrying `candidates` + `provider` (replacing `std::vector<std::vector<std::string>> positionals`); verify with a synthetic-app `buildScope` test in `tests/cmd_completion_emitter_tests.cpp` (same shape as the existing short-only-option case), update the positional assertions the new shape breaks in `model carries positional candidates per subcommand scope` (the `positionals.front()` comparisons and the four `positionals[n].empty()` calls), and confirm the rest of the static emitter tests still pass.

## 2. Slot registration

- [x] 2.1 Register the template name provider on `-T/--template` in `src/cmd/cmd.cpp` with `cfg::Provider{kTemplateNameProvider}` and on the `template` subcommand's `name` positional with `cfg::Provider{kTemplateNameProvider}`; verify in `tests/cmd_completion_emitter_tests.cpp` that the model's `template` value option and the template scope's slot 1 both carry `template-name`.
- [x] 2.2 Record the templateable keys for the `key` positional in `buildAppTree` right after `result.templateTable` is assembled — `completion::recordPositional(templateSub->get_option_no_throw("key"), sortedKeys)` — so the slot stays static; verify the model's template scope slot 2 lists the same key set the template schema declares, that neither `input` nor `state-file` is among them, and update the `_ENCRO_POSCANDS_template` expectation in `emitted scripts cover the full surface deterministically` to the new three-slot string.

## 3. Provider channel

- [x] 3.1 Add `completionCandidates` to `CmdParseResult` and register `--candidates <slot>` on the `completion` subcommand (excluding `--install`/`--uninstall`); dispatch it in `runCompletionCommand` before any shell/install handling and print the slot's values one per line, using `templatestore::list(templatestore::resolveTemplatesDir())` for `template-name`, nothing and exit 0 for an empty set, and a non-zero exit naming the id for an unknown slot; verify `encro completion --candidates template-name` and `encro completion --candidates nosuch-slot` by hand.
- [x] 3.2 Cover the channel in `tests/cmd_completion_command_tests.cpp` with cases for names-one-per-line, empty store, unknown slot, corrupt config file (still exits 0 and lists), an illegal hand-placed stem (`a b.json` beside `anime.json`, only `anime` printed), and a malformed `broken.json` beside a valid template (still lists the valid one), and run them through `xmake test-report --tag="[completion]"`.

## 4. Emission: bash glue and the user-facing docs

- [x] 4.1 Emit provider tables (`_ENCRO_PROV_<id>`, `_ENCRO_POSPROV_<scope>`) and add the bash provider branch: resolve the slot through `${COMP_WORDS[0]} completion --candidates <slot>`, split on lines, filter by `$cur`, and suppress the file fallback when nothing matches or the call fails; verify in `tests/cmd_completion_emitter_tests.cpp` that the emitted bash script carries the provider tables, contains no template name, and stays byte-identical across emissions with and without stored templates.
- [x] 4.2 Extend the bash `[completion][smoke]` case to seed a `TempDir` template store through `ENCRO_CONFIG` (two `template save` invocations), put the build directory first on `PATH`, run the probes from a directory that holds a marker `.json` file (so a broken file-fallback suppression is observable, matching the PowerShell case's `zzz.txt`), and assert that `encro -T `, `encro template show ` and `encro template unset anime ` complete names and keys with no `.json` file names — plus the prefix probes `encro -T a`, `encro template delete a` and `encro template unset anime cr`; then repeat `encro -T ` against an `ENCRO_CONFIG` whose templates directory holds none (the `SUPPRESSED` marker, no file names) and against a failing `encro` stub on `PATH` (same suppression) while `encro --output-format ` and `encro template unset anime ` still complete without the stub; run `ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion][smoke]"`.
- [x] 4.3 Update the README "Shell completion" paragraph to state which candidates are embedded and which are resolved at completion time (stored template names), keeping the re-install-after-upgrade instruction accurate; verify the described behavior by running the emitted script's provider from the shell exactly as the paragraph describes.

## 5. Emission: PowerShell glue

- [x] 5.1 Add the PowerShell provider tables and branch (`$__encroProvider`, `$__encroPosProvider`) resolving through the command name from the AST, returning `CompletionResult`s, and returning the self-echo when the call fails or matches nothing; verify with the same emitted-script assertions on the PowerShell script in `tests/cmd_completion_emitter_tests.cpp`.
- [x] 5.2 Extend the PowerShell `[completion][smoke]` case with the same seeded store, `PATH` prepend, and probe set (names at both slots, keys after `unset <name>`, the prefix probes `encro -T a` / `encro template delete a` / `encro template unset anime cr`, `encro -T ` against an empty templates directory returning no candidates and no file names, and the failing-stub case where `encro -T ` goes quiet while `--output-format` values still complete); run `ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion][smoke]"`.

## 6. Integration

- [x] 6.1 Run the full unit suite (`xmake test-report`) and the opt-in completion suite (`ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"`) and confirm no case regressed.
- [x] 6.2 Re-run `encro completion powershell --install` and `encro completion bash --install` on this machine and confirm the three slots complete from a real prompt, including after saving and deleting a template in the same shell session.

## Planning review

Planning-artifact review (`code-review` planning stage), two lenses, one fresh reviewer and one fresh verification pass. Verdicts below are all `resolved (9eaec8ad)`.

- [critical] `-T/--template` is a free-text value option, which the main-spec requirement "Generated scripts complete enumerated values and config keys" forbids from offering candidates — resolved: that requirement is now a MODIFIED block whose free-text clause reads "neither enumerated values, paths, nor a completion provider", and the proposal's Modified Capabilities names it.
- [critical] `tasks.md` claimed "the untouched static emitter tests still pass" although `model carries positional candidates per subcommand scope` and `emitted scripts cover the full surface deterministically` must change — resolved: task 1.3 and 2.2 now name the assertions to update.
- [warning] "the names `encro template list` reports" contradicted the malformed-template scenario, since `list` aborts on an unparsable file — resolved: reworded to the legal template names present in the templates directory.
- [warning] Name completion was scoped per action while the mechanism is slot-level, so `list`/`path`/`schema` also get names and every action gets keys at the second slot — resolved: both requirements say "whatever action was typed", with a scenario for `encro template list <TAB>` and the noise accepted in design's Risks.
- [warning] Prefix-filtering scenarios had no covering task — resolved: the three prefix probes are in both smoke tasks.
- [warning] The README deliverable was missing from the proposal's Impact — resolved: added as a Documentation bullet.
- [warning] The bash smoke assertion "no `.json` file names" could not detect a broken file-fallback suppression — resolved: that task now runs the probes from a directory holding a marker `.json` file.
- [warning] Design claimed the generator "today only knows about generic metadata", contradicted by the hardcoded `config` action-verb branches — resolved: the rationale now says the config verbs are already hardcoded and a provider id avoids a set of per-shell special cases.
- [warning] "Names are `[a-z0-9._-]`, so no quoting is needed" rested on an invariant `templatestore::list` does not enforce — resolved: the channel filters stems through `templatestore::normalizeName`, with a scenario for a hand-placed `a b.json`.
- [note] Verification pass flagged two scenarios with no run-time probe (empty store, unusable provider, both at `encro -T <TAB>`) — resolved: both smoke tasks now exercise the empty store and the failing stub against `encro -T `.

Not covered by this review: no build or test run (planning artifacts only), so the design's measured latency and the eventual emitted-script behavior are claims to re-check during apply.
