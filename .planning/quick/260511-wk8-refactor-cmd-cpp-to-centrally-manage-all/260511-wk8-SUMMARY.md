---
phase: quick-260511-wk8-refactor-cmd-cpp-to-centrally-manage-all
plan: 01
subsystem: cmd
tags: [refactor, data-driven, cli, cli11]
requires: []
provides: [centralized-cli-flag-management]
affects: [src/cmd/cmd.cpp]
tech-stack:
  added: [std::span, std::unordered_map, std::function]
  patterns: [data-driven-registration, applyMap-result-population, constexpr-flag-arrays]
key-files:
  created: []
  modified: [src/cmd/cmd.cpp]
decisions:
  - "CmdFlagDef uses std::string_view fields; CLI11 v2.6.2 requires std::string → convert at call site via const std::string lvalues"
  - "help/version registered on app directly (not in General group) via std::span::subspan(0,2)"
metrics:
  duration: 17min
  completed-date: 2026-05-12T00:15:27+08:00
---

# Quick Task 260511-wk8: Centralized CLI Flag Management Summary

**One-liner:** Refactored `commandLineInit()` from ~155 lines of per-flag inline registration to ~55-line data-driven loop using `CmdFlagDef` arrays + `applyMap`, preserving identical behavior for all 27 CLI flags.

## What Changed

| File | Change | Lines |
|------|--------|-------|
| `src/cmd/cmd.cpp` | Replaced CMDOption/CMDFlags scaffold with CmdFlagDef + 4 constexpr arrays | +109 |
| `src/cmd/cmd.cpp` | Rewrote commandLineInit() body as data-driven registration + applyMap | +161/-128 |

## New Architecture

```
CmdFlagDef struct (5 fields: name, kind, description, defaultValue, expectedMax)
  │
  ├── GeneralFlags[7]  ──┐
  ├── IOFrags[10]      ──┤  registerFlag() loop → CLI::Option* → optRegistry
  ├── ProcessingFlags[7]──┤
  └── FileOpFlags[3]   ──┘
                              │
                              ▼
                         app.parse()
                              │
                              ▼
                    applyMap[27 entries] → CmdParseResult
```

**Adding a new flag now requires exactly:**
1. 1 entry in the appropriate `constexpr` array
2. 1 field in `CmdParseResult`
3. 1 entry in `applyMap`

## Verification Results

| Metric | Result |
|--------|--------|
| Build | ✅ `xmake build` passes |
| cmd tests | ✅ 62/63 pass (1 pre-existing COLUMNS=72 failure) |
| Total tests | 263/266 pass (3 pre-existing failures) |
| applyMap entries | ✅ 27 (one per flag) |
| CMDOption removed | ✅ grep confirms absence |
| Per-flag variables removed | ✅ grep confirms absence |
| `add_flag`/`add_option` calls | ✅ Only in data-driven `registerFlag()` lambda |

## Commits

| # | Commit | Message |
|---|--------|---------|
| 1 | `4842926` | refactor(quick-260511-wk8): define CmdFlagKind, CmdFlagDef, and 4 constexpr flag arrays |
| 2 | `5394c2c` | refactor(quick-260511-wk8): data-driven commandLineInit() with CmdFlagDef arrays and applyMap |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] CLI11 v2.6.2 requires std::string, not std::string_view**
- **Found during:** Task 2 build
- **Issue:** `add_flag()` and `add_option()` in CLI11 v2.6.2 do not accept `std::string_view`; `add_option` template requires const lvalue for description parameter
- **Fix:** Convert `def.name`/`def.description` to `const std::string` lvalues at call site in `registerFlag()` lambda
- **Files modified:** `src/cmd/cmd.cpp`
- **Commit:** `5394c2c`

None - plan executed exactly as written.

## Known Stubs

None. All flags are fully wired through data-driven registration and result population.

## Self-Check

- [x] `src/cmd/cmd.cpp` exists and compiles
- [x] Commit `4842926` exists (Task 1)
- [x] Commit `5394c2c` exists (Task 2)
- [x] All 27 applyMap entries present
- [x] CMDOption/CMDFlags fully removed
- [x] No per-flag individual variables remain

## Self-Check: PASSED
