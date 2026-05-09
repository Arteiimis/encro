# Feature Landscape: CLI Migration + Terminal Color

**Domain:** CLI tool — option parsing migration + semantic terminal color extension
**Researched:** 2026-05-09

## Table Stakes

Features users expect. Missing = product feels incomplete.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| All 26 existing CLI options preserved | Zero behavioral regression — users' muscle memory and scripts must not break | Low | Direct 1:1 mapping from boost::po to CLI11. Same short/long flags, same defaults, same validation. |
| `--help` / `-h` still works | Universal CLI expectation | Low | CLI11's built-in `--help` flag with custom formatter |
| `--version` added | Table stakes for mature CLI tools. Currently missing. | Low | `app.set_version_flag("-V,--version", "1.6.0")` with terminal::println() for colored output |
| `--color auto\|always\|never` still works | Existing feature, must not regress | Low | Extract from CLI11 result, pass to new `configureFromColorString()` |
| Error messages on invalid input | Users must know what they typed wrong | Low | CLI11's built-in error handling + existing `terminal::println(Error, ...)` |
| Colored error/warning output | Already works via `terminal::` layer. Must survive migration. | Low | Existing `terminal::println()` calls in prelude.cpp, app_entry.cpp are unchanged |
| No regression in 3033 test assertions | Regression safety net | Med | 26 test assertions touch `vm.count()` directly — need rewrite. Remaining 3007 assertions should pass unchanged. |

## Differentiators

Features that set product apart. Not expected, but valued.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Fully colored `--help` output | Most CLI tools have plain or partial help coloring. Full semantic coloring (usage line, group headers, flag names, descriptions) is rare and signals polish. | Med | Requires `formatter_fn` lambda wiring through `terminal::println()`. CLI11 provides the hook; the work is in formatting logic. |
| Semantic color that respects NO_COLOR | Not all CLI tools respect NO_COLOR. This project already does — extending it to help output maintains the standard. | Low | `terminal::colorsEnabled()` already handles NO_COLOR, isatty(), TERM=dumb, Windows VT. New MessageKind values inherit this automatically. |
| Colored `--version` output | Table stakes feature but with colored treatment — version string rendered with semantic Version style. | Low | One `terminal::println(Version, ...)` call in the version flag callback. |
| Consistent semantic color vocabulary | Error=red, Warning=yellow, Success=green, Info=steel_blue — same colors everywhere (logs, help, prompts, version). Builds user trust. | Low | All wired through `MessageKind` enum. Adding help sections extends the vocabulary rather than inventing new ad-hoc colors. |

## Anti-Features

Features to explicitly NOT build.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| Color output via raw ANSI escape sequences | No NO_COLOR support, no isatty detection, no Windows VT handling. Fragile. | Use existing `terminal::println()` with new MessageKind values |
| New color dependency (rang, etc.) | rang has known bugs (style::reset #133, no NO_COLOR #140). Unnecessary — fmtlib already covers all color needs. | Extend existing `terminal::styleFor()` mappings |
| Per-option color customization | Overengineered. 26 options don't need individual color control. Consistent semantic coloring is better UX. | MessageKind enum provides exactly the right granularity (Usage, OptionGroup, OptionName, OptionDesc) |
| Config file support for CLI options | Out of scope for this milestone. CLI11 supports TOML config files but no user demand exists yet. | Defer to future milestone if requested |
| Subcommands | encro is a flat CLI (no `encro encode`, `encro pack` subcommands). Adding subcommands would be a breaking UX change. | Keep flat option structure as-is |

## Feature Dependencies

```
CLI11 added to xmake.lua
    → All option definitions rewritten
        → vm.count()/vm.at() call sites adapted
            → config_builder.cpp compiles
    → formatter_fn lambda wired
        → New MessageKind values needed (Usage, OptionGroup, OptionName, OptionDesc)
            → styleFor() extended
                → Colored --help output works
    → configureFromVariablesMap() replaced by configureFromColorString()
        → --color flag still works

--version flag added
    → MessageKind::Version needed
        → styleFor() extended
            → Colored --version output works
```

## MVP Recommendation

Prioritize:
1. CLI11 integration (option definitions + xmake dependency) — prerequisite for everything
2. Terminal color extension (new MessageKind values + styleFor() + configureFromColorString()) — prerequisite for colored help
3. Colored `--help` + `--version` — the visible user-facing value
4. All 32 `vm.count()`/`vm.at()` call sites adapted — required for compilation
5. Tests rewritten — required for confidence

**Defer:** Boost dependency narrowing — out of scope, no user-visible benefit in this milestone. Progress bar coloring — separate concern (indicators library has its own color system), needs separate evaluation.

## Sources

- [Context7 /cliutils/cli11] — formatter_fn, option groups, add_option API, AppFormatMode
- [GitHub Releases: CLIUtils/CLI11 v2.6.2] — C++26 compatibility fixes, changelog
- [Project source: src/cmd/cmd.cpp] — Current 26 options across 4 option groups
- [Project source: src/infra/terminal.{h,cpp}] — Existing MessageKind enum, styleFor(), colorsEnabled()
- [Project source: src/cmd/config_builder.cpp] — 32 vm.count()/vm.at() call sites
