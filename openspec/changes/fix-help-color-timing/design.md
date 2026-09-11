## Context

Help text is currently rendered to a final string inside `buildAndParse` (`src/cmd/cmd.cpp`): the success path calls `tree.app->help()` right after `app->parse`, and the `CLI::CallForHelp` catch picks the matching subcommand's `help()`. The formatter's `terminal::styledText` calls query the global color mode (`terminal::colorsEnabled`), which `prelude::initStartup` sets from `result.color` only after `commandLineInit` returns (`src/app/prelude.cpp:34`). So every help rendered today colors against the process-initial `auto` mode. The CLI11 app objects backing the render are intentionally leaked for the process lifetime (see `AppTree` in `src/cmd/cmd.h`), which is the constraint that makes deferral cheap.

## Goals / Non-Goals

**Goals:**

- Every help-flag path (main `-h`/`-hh`, `preview`/`organize`/`config`/`completion` `-h`) renders against the color mode in effect at print time.
- The fix lives in one place (parse result construction) so all consumers — `app_entry` help path, bare `config`, bare `completion` — are covered without each site re-rendering.

**Non-Goals:**

- No change to which text is rendered, help layout, tier selection, `(=default)` display, streams, or exit codes.
- No change to config injection semantics, `--color` validation, or the invalid-`--color`-with-`-h` error path (parse-time validation still runs and errors before any help prints).
- No reworking of `terminal::` APIs or the color-mode precedence itself.
- Bare `encro config` and bare `encro completion` do NOT gain config-honoring color: they return from the probe parse, which deliberately skips config injection so a corrupt config file cannot break them. Their help renders under `auto` (spec excludes them). Making them honor the stored color would require re-reading the config on the probe path — the double-load this design rejects.

## Decisions

### D1: Lazy renderer on `CmdParseResult` (chosen)

Replace the `std::string helpText` field with a `std::function<std::string()>` stored under a distinct member name (e.g. `helpRenderer_`, following the repo's trailing-underscore member convention — a field and its accessor cannot share a name), plus a `helpText()` accessor that invokes it. The eight assignment sites in `buildAndParse` (post-parse main help; config and completion got-subcommand paths; four `CallForHelp` catch branches; `ParseError` catch) each install a lambda capturing the corresponding `CLI::App*` from that parse's `AppTree` and calling `->help()`.

- Why safe: the `CLI::App` objects are deliberately leaked and outlive the process's parse-and-dispatch flow (existing design comment in `cmd.h`); the formatter is stateless with respect to invocation order, and all config-injected defaults are already applied to the option objects before parse returns.
- Why this shape: consumers keep a single call site (`cmd.helpText()`), tests keep a single field-access to update, and the rendered string is produced exactly once per process at the moment it is printed — after `prelude` has configured the color mode.
- Alternative rejected — re-render in `prelude` after configure: `prelude` does not see the `AppTree`, and `tests/test_utils.h::parseArgs` calls `commandLineInit` directly, so the stale early-render string would remain observable to every other caller; the bug would survive outside the `prelude` path.
- Alternative rejected — pre-configure the color mode before parse by peeking at argv/config: duplicates config loading, races the established injection design (config values become CLI11 forced defaults consumed at parse), and still special-cases one key instead of fixing the render-vs-configure ordering.

### D2: Accessor keeps the rendered string materialized at most once

`helpText()` caches the result after the first call. Help is printed at most once per process; the cache only guards against accidental double-render divergence if a future caller reads twice. Cost is one string copy. Test sites that compare two parses' help (e.g. `cmd_help_tiering_tests.cpp`'s brief-vs-full combination checks) call the accessor once per result and compare the returned strings, so the cache is invisible to them.

### D3: Test strategy mirrors the bug and the runner's TTY reality

`xmake test-report` runs tests with piped (non-TTY) output, so under `auto` no ANSI is ever produced — a naive "configure Never after parse, expect no ANSI" test is green on current code. The red tests must therefore force the asymmetry:

- never-direction: `terminal::configure(Always)` BEFORE `parseArgs` (current code eagerly renders with Always → ANSI present in the string), then `configure(Never)`, then assert the accessor output is ANSI-free. Red today (string already carries Always-mode escapes), green after the fix.
- always-direction: `parseArgs` first, then `configure(Always)`, then assert the accessor output DOES contain ANSI. Red today (string was rendered under piped-auto without escapes), green after the fix.

The existing color-invariance test (`tests/cmd_cmd_tests.cpp`, "Help layout is color-mode invariant" section) must be restructured: with lazy rendering, deferring both accessor calls until after `terminal::reset()` would render both sides under the same mode and pass vacuously. Each side must materialize its string (call the accessor) while its own mode is still configured, then compare. Companion cases: CLI-level `--color never -h` / `--color always -h` through `parseArgs` plus `configureFromColorString(result.color)`; config-file `color = never` via the existing `ENCRO_CONFIG` override + `parseArgs` asserts the injected `result.color == "never"` and ANSI-free help after configure. Existing tests that configure the mode before `parseArgs` keep their behavior but switch to the accessor call.

## Risks / Trade-offs

- [Test churn: ~39 `.helpText` references across five test files become accessor calls] → Mechanical update; no assertion semantics change except the invariance test restructure (D3).
- [Renderer captures leaked pointers — an `AppTree` consumer could theoretically free them] → No code path frees the app (documented intentional leak); note the capture contract in the field comment.
- [Deferred render hides formatter exceptions until print time] → The formatter is pure string assembly over parsed options; `help()` is already called on every path that prints, and CLI11's own help does not throw here today.
- [Probe parse in `commandLineInit` builds a second app; renderer must capture the second tree] → Assignment happens inside `buildAndParse`, so each returned result carries renderers bound to its own tree by construction; no cross-tree capture is possible.
- [Bare `config`/`completion` help stays auto-colored] → Documented spec exclusion; unit tests cannot observe the gap anyway (piped runs auto-suppress), and fixing it would trade corrupt-config resilience.
