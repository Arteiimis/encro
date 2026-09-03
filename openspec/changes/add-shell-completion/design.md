# Design: add-shell-completion

## Context

The CLI is registered declaratively as `OptSpec` tables and built into a CLI11 app tree inside `buildAndParse` (`src/cmd/cmd.cpp`); option groups are CLI11 subcommand nodes with empty names, and the real subcommands are `preview` and `config` (see the filter precedent at `cmd.cpp:937`). Two existing mechanisms are reused here: the config-key capture precedent (`cfg::ConfigKey` in `src/cmd/option_specs.h` registers option pointers into a side-channel registry) and the per-subcommand help formatter precedent (`makeSubcommandHelpFormatter`). CLI11 v2.7.2 exposes the mutual-exclusion graph symmetrically via `Option::get_excludes()` (the library registers both directions in `Option::excludes`, `Option_inl.hpp:214-221`), but does not expose `IsMember` candidate sets, has no completion support of its own, and its flag names carry negation notation (`--no-keep{false}`). See proposal.md — Why for motivation.

## Goals / Non-Goals

**Goals:**

- Single source of truth: emit from the same registration tree the parser uses; no hand-maintained option lists.
- Deterministic, side-effect-free emission (no config reads, no user-file writes on the print path).
- One-command install/uninstall that is safe to re-run and trivially reversible.
- Exclusion-aware candidate filtering using the parser's own exclusion graph.

**Non-Goals:**

- cmd.exe/Clink, zsh, fish shells.
- Per-keystroke dynamic completion protocol (`__complete`); candidate data is static by construction.
- `Needs`-based candidate hiding (parse is order-independent; hiding would block valid input orders).
- `-h`/`-hh` tier distinction inside completion (all options complete).
- Custom MSYS HOME layouts (install targets `%USERPROFILE%`; see Risks).

## Decisions

### D1: Emit from a shared app-tree builder, not from parse

Extract the registration portion of `buildAndParse` into a `buildAppTree(CmdParseResult&)` that both the normal parse path and the completion emitter call. The completion path parses `encro completion ...` with config injection disabled (`injectConfig=false`), so config load errors and stored defaults cannot affect emission (spec: config-independence). The existing intentional-leak pattern for the app object (config-command registry keeps pointers, `cmd.cpp:954`) carries over. Alternative rejected: parsing a fully independent argv shape — would duplicate registration and defeat the single-source-of-truth goal.

### D2: Candidate registry captured at registration time

Extend the capture precedent in `src/cmd/option_specs.h`: `cfg::Members` and `cfg::CheckedTransformer` register `long-name → candidates`; `cfg::Range`, `cfg::PositiveNumber`, `cfg::NonNegativeNumber` register a `numeric` marker; `cfg::ConfigKey` additionally records `key → long-name` so `config --set <key> <value>` can complete the bound option's candidates. The registry is a new small module (e.g. `src/cmd/completion_registry.h/.cpp`). Classification falls out of capture: value options with candidates = enum, numeric marker = numbers, an explicit path-option name list in the emitter (cross-checked against the live tree by a unit test) = file delegation, and every remaining value option (free text such as `--video-codec`) = no value candidates. Alternative rejected: introspecting `Option::get_validator(index)` — `Validator` does not publicly expose the `IsMember` value set, and digging into its internals is fragile across CLI11 upgrades.

### D3: Exclusion filtering straight from CLI11

At emission, walk each command scope (main scope after filtering out empty-name group nodes; each named subcommand's own scope) and read `Option::get_excludes()`. Convert the exclusion graph into name sets: each hidden candidate maps to the union of all names (short, long, negation, `{...}` stripped) of the options that exclude it. The shell glue hides a candidate when any of those trigger names appears verbatim among the typed words. CLI11 already symmetrizes the graph, so one-sided declarations (`--resume` excludes `--restart` only) produce symmetric filtering with no extra logic.

### D4: Per-shell glue as literal templates with data slots

Each shell's script is a compile-time literal template with emitted data tables (option names per context, subcommand names, candidate maps, exclusion name-sets, path-option list) substituted at emission. Bash glue: completion function reading `COMP_WORDS`/`COMP_CWORD`, `compgen -A file` for path options, `-o default` retained so unhandled positions fall back to file completion. PowerShell glue: `Register-ArgumentCompleter` script block receiving `$words`/`$wordToComplete`, returning `[System.Management.Automation.CompletionResult]` values. No templating dependency; output is plain string assembly (determinism requirement).

### D5: Install locations and wiring

Per-user root reuses the configstore directory resolution precedent (`resolveConfigPath`, `config_store.cpp:115`: `LOCALAPPDATA` → `APPDATA` → `~/.config`), with completion assets under a `completion/` subdirectory.

- **PowerShell**: script written to `<encro-user-dir>/completion/encro.ps1`. Wiring appends a marker-guarded block (`# >>> encro-completion >>>` … `# <<< encro-completion <<<`) containing a dot-source line to every PowerShell profile that already exists among `{Documents\WindowsPowerShell\Microsoft.PowerShell_profile.ps1, Documents\PowerShell\Microsoft.PowerShell_profile.ps1}`; if neither exists, the PowerShell 7+ location is created. `Documents` is resolved via `SHGetKnownFolderPath(FOLDERID_Documents)` so OneDrive-redirected Documents folders work (may require adding the shell32 sys-link — verify in xmake).
- **Bash**: when the bash-completion framework is detected (user dir `%USERPROFILE%\.local\share\bash-completion\completions` or a Git-for-Windows installation's `usr\share\bash-completion` exists), the script is written directly as `...\completions\encro` (lazy-loaded, startup file untouched). Otherwise the script goes to the encro user dir and a marker-guarded source block is appended to `%USERPROFILE%\.bashrc`. All bash-target files are written with LF endings.

### D6: Idempotency and uninstall mechanics

Install: if the installed script file already exists with identical content → report "already current" and only repair missing wiring; if content differs → overwrite the file. Wiring is scanned by markers, never duplicated. Uninstall: remove the marker block from each candidate startup file and delete the installed script; missing pieces are no-ops. Both flags together is rejected by the same declarative exclusion mechanism the rest of the CLI uses. On success both commands print the installed script path and the wiring locations touched; the no-op cases report "already current" / "nothing installed" respectively.

### D7: Testing strategy

- **Unit** (Catch2): emitter content per shell (every registered name appears, `{...}` stripped, candidates/exclusion tables present); registry cross-check (every captured long-name resolves in the live tree); determinism (two emissions byte-identical); install/uninstall against `TempDir` with redirected environment (reuse the `processenv` indirection so tests inject `USERPROFILE`/HOME and the known-folder probe), covering no-op, refresh, marker-block removal, and untouched surrounding content; both-flags usage error.
- **Smoke** `[smoke]` (real shells when on PATH, `SKIP()` otherwise — precedent: real-ffmpeg tests): bash sources the wired startup file and drives the completion function with synthetic `COMP_WORDS`, asserting candidates (e.g. `--output-format` → `mp4 webp`); PowerShell calls `TabExpansion2` on a synthetic input line and asserts results. These tests also pin the PowerShell file-completion fallback behavior (see Risks).

### D8: Subcommand CLI surface

`encro completion` — description `print, install, or uninstall shell completion scripts` (matches the cli-help-layout delta); positional `shell` constrained to `{powershell, bash}`; flags `--install` / `--uninstall` (mutually exclusive); bare invocation shows the subcommand help, mirroring `config`'s bare-help behavior. Own help formatter via `makeSubcommandHelpFormatter`.

## Risks / Trade-offs

- [PowerShell may not fall back to native file completion when a registered completer returns nothing] → Resolved by the task-1.1 spike: under both Windows PowerShell 5.1 (5.1.26100) and PowerShell 7.6.3, `TabExpansion2` falls back to native file completion when a registered native completer returns an empty result set, and suppresses the fallback when the completer returns any candidate. Path options therefore return no candidates; no directory enumeration is needed in the glue.
- [Custom MSYS HOME makes Git Bash look somewhere other than `%USERPROFILE%`] → Document the limitation in install output hints; install targets `%USERPROFILE%`, which is Git-for-Windows' default HOME. A future `--bash-home` override can be added without spec changes.
- [CRLF endings corrupt the appended `.bashrc` block] → Bash-target writes force LF explicitly; the smoke test sources the wired file and would fail on carriage-return artifacts.
- [Script snapshots go stale after an encro upgrade] → Emission path is always current; install overwrites on re-run and its output reminds about re-running after upgrades.
- [Registry capture can drift from option names] → Unit cross-check requires every captured long-name to resolve against the live tree; a renamed option breaks the test, not a user's shell.

## Migration Plan

New capability; no existing behavior changes except the main-help commands section (covered by the cli-help-layout delta). Rollback is `git revert` plus `encro completion <shell> --uninstall` for installed users.

## Open Questions

None — the PowerShell fallback spike (D7/Risks) is scheduled as the first implementation task and cannot change the specs or the approach.
