---
title: CLI Library Selection — boost::program_options → CLI11
date: 2026-05-09
context: /gsd-explore CLI library comparison for colored --help output
---

## Motivation

Two pain points with current `boost::program_options`:

1. **Help output coloring** — no built-in support. Formatter is sealed, ANSI injection limited to description strings only. Full colored output requires walking `options_description::options()` vector manually.
2. **Option definition API** — `operator()` chaining is disliked. Heavy use of `value<T>()->default_value()->required()` etc.

## Libraries Compared

| | boost::program_options | CLI11 | cxxopts |
|---|---|---|---|
| Built-in color | No | No | No |
| Help customization | Sealed `print()` | `formatter_fn(lambda)` full replacement | `set_width()` only, no hook |
| Option definition | `operator()` chaining | Fluent `.add_option()->required()->expected()` | Fluent `add_options()(...)` |
| Groups | `options_description("name")` | `add_option_group("name","desc")` + constraints | `add_options("Group Name")` display-only |
| Migration effort | — | ~138 lines cmd.cpp + ~30 vm call sites | Similar |

## Decision: CLI11

CLI11 chosen because:

- `formatter_fn` lambda gives full control over help output rendering — ideal for injecting `terminal::println()` calls
- Fluent option definition API replaces disliked `operator()` chaining
- No new dependency needed for coloring — project's existing `terminal::` wrapper + fmtlib covers it

## Color Approach: Reuse existing `terminal::` layer

Project already has a complete semantic color system in `src/infra/terminal.h`:

- `MessageKind` enum (Error/Warning/Success/Info/Hint/Heading)
- `styleFor()` maps semantics to `fmt::text_style`
- `colorsEnabled()` handles NO_COLOR, isatty(), TERM=dumb, Windows VT

**Plan:** Extend `MessageKind` with help-section semantic values (Usage, OptionGroup, OptionName, OptionDesc), then call `terminal::println()` inside CLI11's `formatter_fn`. Zero new dependencies.

Alternatives considered and rejected:
- **rang**: single header, but no NO_COLOR support (open issue #140), known style::reset bug (#133)
- **Hand-written ANSI**: reinventing wheel, no semantic layer
- **Raw fmt::fg()**: already wrapped by terminal::, no reason to bypass

## Migration Scope

- `src/cmd/cmd.cpp` — 138 lines, complete rewrite from boost::po to CLI11
- `src/cmd/cmd.h` — update `CmdParseResult` to use CLI11 types
- `src/cmd/config_builder.cpp` — adapt ~17 `vm.count()`/`vm.at()` calls
- `src/infra/terminal.cpp` — adapt `configureFromVariablesMap()`  
- `src/app/prelude.cpp` — adapt 2 `vm.count()` calls
- `src/utils/utils.cpp` — adapt `getParamStr()`  
- `src/app/app_entry.cpp` — adapt `vm.count("help")` and help output
- `tests/cmd_cmd_tests.cpp` — rewrite CLI parsing tests
- `tests/cmd_config_builder_tests.cpp` — adapt from `po::variables_map` to CLI11 result
- `xmake.lua` — add CLI11 dependency, narrow boost to only uuid/lexical_cast if possible
