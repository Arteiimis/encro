# Phase 19: CLI11 Migration - Context

**Gathered:** 2026-05-09
**Status:** Ready for planning

## Phase Boundary

Replace `boost::program_options` with CLI11 for CLI argument parsing. All 26 options across 4 groups produce identical parsing behavior — same defaults, same validation, same help text layout. Zero user-visible behavioral change. Color changes are explicitly out of scope (Phase 20).

Requirements: CLI11-01 through CLI11-05.

## Implementation Decisions

### Migration Strategy
- **D-01:** Direct CLI11 API at each call site — no compatibility shim / boost::po wrapper. The 34 `vm.count()`/`vm.at()`/`getParamStr()` references across 5 consumer files are migrated directly to CLI11 access patterns (`app.count("--flag")`, `*app["--key"]`, `app["--key"]->as<T>()`).
- **D-02:** `CmdParseResult` replaced with a struct holding CLI11 parse results. `buildConfig()` accepts a clean results struct (not `CLI::App*` or CLI11-specific types) — decoupling config logic from the parsing library.

### formatter_fn Help Layout
- **D-03:** Help text rendering uses helper functions + orchestrating formatter_fn lambda:
  - `formatOptionHelp(option, nameWidth)` — single option line with name, type placeholder, description
  - `formatGroupHeader(name)` — group title section
  - `makeHelpFormatter(lineLength, minDescLength)` — returns the `CLI::FormatterFcn` lambda that walks groups/options and calls helpers
- **D-04:** No color in Phase 19 — helpers return plain text. The structure is designed for Phase 20 to inject `terminal::println()` calls by changing what helpers emit.
- **D-05:** `resolveHelpTextLayout()` ported from boost::po context to CLI11 context. Console-width-aware column sizing preserved.

### Error Message Compatibility
- **D-06:** Parse-level error messages from CLI11 accepted as-is (e.g., "The following argument was not expected: --foo" instead of boost::po's "unrecognised option '--foo'"). No wrapping/rewriting.
- **D-07:** All config-level validation error messages in `config_builder.cpp` are unchanged — they operate on parsed results, not on the parsing library.

### Test Strategy
- **D-08:** `cmd_cmd_tests.cpp` — integration tests: parse real argv[] strings through the migrated `commandLineInit()`, verify results. True end-to-end parser testing.
- **D-09:** `cmd_config_builder_tests.cpp` — fixture-based unit tests: construct results struct directly, bypass the parser. Test config validation/transformation in isolation.

### Folded Todos
- **migrate-cli11.md** — "Migrate from boost::program_options to CLI11" (score 0.6). All 16 checklist items are covered by Phase 19 scope. Folded into planning.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Architecture & Stack
- `.planning/codebase/ARCHITECTURE.md` — CLI→Config→Pipeline flow, component responsibilities, data flow
- `.planning/codebase/STACK.md` — Current boost::program_options usage, dependency locations
- `.planning/codebase/INTEGRATIONS.md` — All 26 CLI flags documented with purpose

### Requirements
- `.planning/REQUIREMENTS.md` §v1.6 — CLI11-01 through CLI11-05 (5 requirements for Phase 19)
- `.planning/ROADMAP.md` §Phase 19 — Success criteria: identical parsing, help layout, 3033 assertions pass, boost::po removed from xmake.lua

### Design & Research
- `.planning/notes/cli-library-selection.md` — CLI11 vs cxxopts comparison, decision rationale, migration scope per file
- `.planning/research/SUMMARY.md` — CLI11 formatter_fn API, pitfall #1 (complete replacement not decorator), Phase 20 dependency notes
- `.planning/STATE.md` — Phase 19 blockers: formatter_fn complexity (60-80 line budget), 58 vm call sites, adapter pattern recommendation

### Source Files (MUST READ during planning)
- `src/cmd/cmd.h` — Current `CmdParseResult` struct (po::variables_map)
- `src/cmd/cmd.cpp` — 138 lines, 4 option groups, 26 options, `resolveHelpTextLayout()`
- `src/cmd/config_builder.h` — `buildConfig(variables_map const&)` signature
- `src/cmd/config_builder.cpp` — 414 lines, 29 vm references, all config validation
- `src/app/prelude.cpp:64-65` — 2 vm references (verbose, verbose-echo)
- `src/infra/terminal.cpp:160-161` — 1 vm reference (color → configureFromColorString)
- `src/utils/utils.cpp:366` — 1 vm reference (getParamStr template)
- `src/app/app_entry.cpp:104` — 1 vm reference (help)
- `tests/cmd_cmd_tests.cpp` — 244 lines, CLI parsing tests
- `tests/cmd_config_builder_tests.cpp` — 732 lines, config builder tests

## Existing Code Insights

### Reusable Assets
- `resolveHelpTextLayout()` in `src/cmd/cmd.cpp:30-41` — adaptive column width based on `consolewidth::resolveColumns()`. Port to CLI11 context with minimal changes.
- `readProcessType()`, `readOutputFormat()`, `readOutputLayout()` etc. in `config_builder.cpp` — these internal helpers are the natural migration boundary. They currently take `vm`, will take results struct fields instead.

### Established Patterns
- `eh::Result<T>` error handling throughout cmd module — preserve this pattern
- `appctx::AppConfig` plain struct populated by `buildConfig()` — no changes needed
- 4 option groups (General, Input/Output, Processing, File operation) — preserved verbatim in CLI11 via `add_option_group()`
- Help text with adaptive column width via `consolewidth::resolveColumns()` — preserved

### Integration Points
- `prelude::initStartup()` in `src/app/prelude.cpp` — calls `commandLineInit()`, receives `CmdParseResult`, extracts `vm` for verbose/color setup. API change: `CmdParseResult` struct shape changes.
- `appentry::run()` in `src/app/app_entry.cpp` — calls `commandLineInit()`, checks `vm.count("help")`, prints help via `std::cout << desc`. API change: help output goes through `formatter_fn` instead of `operator<<`.
- `cmd::buildConfig()` in `src/cmd/config_builder.cpp` — signature changes from `variables_map const&` to results struct. All 29 internal vm references adapted.

## Specific Ideas

- `formatter_fn` estimated at 60-80 lines total (matches STATE.md research budget)
- Option group ordering: General → Input/Output → Processing → File operation (same as current)
- `--help` / `-h` handled by CLI11's built-in help flag — overridden with `formatter_fn` for layout control
- `allow_unregistered()` in CLI11 (equivalent to boost::po's allow_unregistered) — needed during migration to handle flags consumed by prelude before full parse
- `configureFromColorString()` replaces `configureFromVariablesMap()` — wire through the results struct instead of vm

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 19-CLI11 Migration*
*Context gathered: 2026-05-09*
