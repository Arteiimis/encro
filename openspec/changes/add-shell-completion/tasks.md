# Tasks: add-shell-completion

## 1. Spike and tree refactor

- [x] 1.1 PowerShell `TabExpansion2` spike: verify whether a registered completer returning nothing falls back to native file completion; record the outcome and the chosen path-option strategy in design D5/D7. Verify: spike script runs under `powershell` and its conclusion is noted in this change's design.md.
- [x] 1.2 Extract `buildAppTree` from `buildAndParse` so registration is callable without config injection; no behavior change. Verify: existing `[cmd]` unit tests pass (`xmake test --tag="[cmd]"`).

## 2. Candidate registry

- [x] 2.1 Add the completion registry module (long-name → candidates / numeric marker / file-delegation default; config-key → long-name map) with unit tests. Verify: new registry unit tests pass (`xmake test --tag="[cmd]"`).
- [x] 2.2 Capture candidates in `option_specs.h` tokens (`Members`, `CheckedTransformer`, `Range`, `PositiveNumber`, `NonNegativeNumber`, `ConfigKey`). Verify: cross-check unit test passes — every captured long-name resolves against the live app tree.

## 3. Emitter core

- [x] 3.1 Implement the tree walk: per-context option names (short/long/negation, `{...}` stripped), empty-name group filtering, subcommand scoping. Verify: unit tests assert main vs `preview` vs `config` candidate sets.
- [x] 3.2 Convert the exclusion graph via `get_excludes()` into trigger/candidate name sets. Verify: unit test proves one-sided declarations (`--resume`/`--restart`, `-i`/`--inputs`) yield symmetric name sets.
- [x] 3.3 Assemble the deterministic per-shell data model and wire the `injectConfig=false` emission path. Verify: unit tests show two emissions byte-identical and identical output with/without a stored or corrupt config file.

## 4. Shell script templates

- [x] 4.1 Bash glue template: completion function over `COMP_WORDS`/`COMP_CWORD`, exclusion filtering, `compgen -A file` delegation, LF output. Verify: unit tests assert emitted content covers all registered names, path delegation list, and exclusion table.
- [x] 4.2 PowerShell glue template: `Register-ArgumentCompleter` block with `CompletionResult` emission, exclusion filtering, path strategy per spike 1.1. Verify: unit tests assert emitted content mirrors the bash assertions for the PowerShell dialect.

## 5. Subcommand wiring

- [x] 5.1 Register the visible `completion` subcommand (positional `shell` limited to `powershell|bash`, `--install`/`--uninstall` mutually exclusive, bare invocation shows help, own help formatter) and update the main-help commands-section test. Verify: `[cmd]` parse tests cover shell validation, bare-help behavior, and `encro -h` listing `completion` with its description.

## 6. Install and uninstall

- [x] 6.1 PowerShell install: write script to the encro user dir, wire marker-guarded dot-source blocks into existing profiles (known-folder Documents resolution; create the PowerShell 7+ profile when none exists). Verify: unit tests with `TempDir` + redirected environment cover first install, no-op re-run, and in-place refresh with a single activation entry.
- [x] 6.2 Bash install: lazy-load completions directory when bash-completion is detected, otherwise guarded `.bashrc` source block, all writes LF. Verify: unit tests cover both branches and that pre-existing startup-file content is preserved byte-for-byte.
- [x] 6.3 Uninstall and usage errors: remove marker blocks and installed files, no-op when absent, `--install --uninstall` rejected without side effects. Verify: unit tests cover full install→uninstall round-trip, no-op uninstall, and the both-flags error.

## 7. Real-shell smoke tests

- [x] 7.1 `[smoke]` bash: source the installed/wired script in real bash, drive the completion function with synthetic `COMP_WORDS`, assert candidates (e.g. `--output-format` → `mp4 webp`, `--resume` hides `--restart`). Verify: test passes when bash is on PATH, auto-skips otherwise.
- [x] 7.2 `[smoke]` powershell: call `TabExpansion2` on synthetic input lines and assert results (option names, enum values, path fallback per spike 1.1). Verify: test passes when PowerShell is on PATH, auto-skips otherwise.

## 8. Docs and full verification

- [ ] 8.1 README: add an "enable completion" section with the per-shell install command and the re-run-after-upgrade note. Verify: README section renders and commands match the implemented CLI.
- [ ] 8.2 Full-suite run and change validation. Verify: `xmake test-parallel` is green and `openspec validate add-shell-completion` passes.
