---
title: Migrate from boost::program_options to CLI11
date: 2026-05-09
priority: medium
source: /gsd-explore CLI library comparison
resolves_phase: 19
---

## Description

Replace `boost::program_options` with CLI11 for CLI argument parsing. Use existing `terminal::` semantic color layer for `--help` output coloring.

## Tasks

- [ ] Add CLI11 dependency to xmake.lua
- [ ] Extend `terminal::MessageKind` enum with help-section variants (Usage, OptionGroup, OptionName, OptionDesc)
- [ ] Rewrite `src/cmd/cmd.cpp` — 26 options, 4 groups, parsed via CLI11 API
- [ ] Update `src/cmd/cmd.h` — `CmdParseResult` to hold CLI11 types instead of `po::variables_map`
- [ ] Implement `formatter_fn` lambda in CLI11 using `terminal::println()` for colored help
- [ ] Adapt `src/cmd/config_builder.cpp` — ~17 vm reads to CLI11 result access
- [ ] Adapt `src/infra/terminal.cpp` — `configureFromVariablesMap()` to CLI11 equivalent
- [ ] Adapt `src/app/prelude.cpp` — 2 vm.count() calls
- [ ] Adapt `src/utils/utils.cpp` — `getParamStr()`
- [ ] Adapt `src/app/app_entry.cpp` — help triggering and output
- [ ] Rewrite `tests/cmd_cmd_tests.cpp` — CLI11 parsing tests
- [ ] Rewrite `tests/cmd_config_builder_tests.cpp` — from fake vm to CLI11 result
- [ ] Preserve adaptive column width logic (`resolveHelpTextLayout()`)
- [ ] Preserve custom intro line behavior
- [ ] Verify all existing assertions pass with zero behavioral regression
- [ ] Narrow boost dependency from `boost[all]` to only needed libraries (uuid, lexical_cast)

## See Also

- `.planning/notes/cli-library-selection.md` — full comparison and decision rationale
- `.planning/seeds/cli-color-deepening.md` — future enhancements after migration
