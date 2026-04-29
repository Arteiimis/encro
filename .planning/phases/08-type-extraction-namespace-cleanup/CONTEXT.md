# Phase 8 Context: Type Extraction & Namespace Cleanup

**Date:** 2026-04-29
**Source:** Discussion phase (gsd-discuss-phase 8)
**Status:** Ready for planning

---

## Phase Overview

Pure mechanical refactoring: extract shared value types to independent headers, move global-scope pollution into `pack::` namespace, break the `packer.h` ↔ `pack_service.h` circular dependency. Zero behavioral change — all 909 assertions must pass.

---

## Current State

- **`pack_service.h`** (82 lines): Defines `PackRunResult`, `PackFileEntry`, `FileOrdinalRange`, `PackPlan` + free functions — all in `pack::`
- **`packer.h`** (126 lines): Line 5 includes `#include "pack/pack_service.h"` (circular dependency). Defines: `ZipEntryNameResolver`, `PackEntryProgressCallback` (aliases), `PackGroupInput`, `PackGroupPartition`, `PackEntryInput`, `PackEntryPartition` (structs) — all in global scope
- **8 consumer files**: `packer.cpp`, `pack_service.cpp`, `video_process.cpp`, `video_output_planning.cpp`, `picture_process.cpp/.h`, `pipeline.cpp`, `archive_plan.h` + 2 test files
- No existing CONTEXT.md, no checkpoint, no SPEC.md

---

## Decisions Resolved

### D-01: Global Alias Treatment
**Verdict:** Move `ZipEntryNameResolver` and `PackEntryProgressCallback` into `pack::detail::` namespace.

*Rationale:* These are implementation details of `packer.h`'s free function signatures, not part of the public pack:: API. Moving to `pack::detail::` follows the v1.2 `videobatch::detail` precedent — internal types accessible for compilation but semantically private. Consumers include `packer.h` and get the aliases via `using pack::detail::ZipEntryNameResolver` if needed.

### D-02: Include Strategy for pack_service.h
**Verdict:** `pack_service.h` MUST `#include "pack_types.h"` — forward declarations insufficient.

*Rationale:* `PackPlan` defines `std::vector<PackGroup> groups` as a member field. `PackGroup` contains `std::vector<PackFileEntry>`. C++ `std::vector<T>` members require complete type of `T` — forward declarations only work for pointers/references. No alternative exists. This creates the correct dependency direction: `packer.h` → `pack_types.h` ← `pack_service.h`, with neither header depending on the other.

### D-03: Compilation Verification
**Verdict:** Standalone compilation test required — a `.cpp` file that includes ONLY `packer.h` and verifies it compiles without `pack_service.h`.

*Rationale:* The 909-assertion test suite exercises pack functionality end-to-end but transitively includes `pack_service.h` through other paths. A standalone compilation unit proves the circular dependency is genuinely broken. Add to `tests/` as `packer_standalone_compile_test.cpp` (not linked into test executable — compile check only).

### D-04: Constant Placement
**Verdict:** Move `kDefaultMaxArchiveGroupSize` from `packer.h` to `pack_types.h`.

*Rationale:* This constant is used by `PackPlan` consumers (default group size for archive planning). Placing it in `pack_types.h` alongside `PackFileEntry` and `PackRunResult` keeps related constants with their types. Consumers that need `PackPlan` also need this constant — single include.

---

## Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| TYPE-01 | Extract shared value types (PackFileEntry, FileOrdinalRange, PackRunResult) to `src/pack/pack_types.h` | Pending |
| TYPE-02 | Move global-scope structs (PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition) to `pack::detail::` in `src/pack/packer_types.h` | Pending |
| TYPE-03 | `packer.h` no longer includes `pack_service.h` — circular dependency resolved | Pending |
| TYPE-04 | All existing code compiles, 909 assertions pass, zero behavioral change | Pending |

---

## Success Criteria

1. All shared value types (PackFileEntry, FileOrdinalRange, PackRunResult) defined in `src/pack/pack_types.h` — usable without `pack_service.h` or `packer.h`
2. Global-scope structs accessible only through `pack::detail::` namespace in `src/pack/packer_types.h`
3. `packer.h` compiles without `pack_service.h` — circular dependency resolved (verified by standalone compile test)
4. Full test suite passes — 909 assertions across 215 test cases with zero failures
5. All existing consumer code compiles unchanged — only `#include` paths adjusted, no logic modified

---

## Implementation Constraints

| Constraint | Source | Detail |
|-----------|--------|--------|
| Zero behavioral change | Phase 8 goal | No logic changes — pure header/include refactoring |
| PackPlan stays aggregate | v1.2 Decision + PROJECT.md | `static_assert(is_aggregate_v)` preserved |
| All 909 assertions pass | Baseline | Test suite unchanged except include adjustments |
| No consumer logic changes | D-01 | Consumer files only have `#include` paths adjusted |
| Free functions unchanged | Phase 8 scope | Only types and namespaces move; function signatures preserved |

---

## Key Files

| File | Role | Action |
|------|------|--------|
| `src/pack/pack_service.h` | PackPlan, PackFileEntry, PackRunResult, FileOrdinalRange | Extract shared types → pack_types.h; add `#include "pack/pack_types.h"` |
| `src/pack/packer.h` | Global structs, aliases, circular include | Remove `#include "pack/pack_service.h"`; add `#include "pack/pack_types.h"`; move structs → packer_types.h; move aliases → `pack::detail::` |
| `src/pack/pack_types.h` | **NEW** — shared value types | PackFileEntry, FileOrdinalRange, PackRunResult, `kDefaultMaxArchiveGroupSize` |
| `src/pack/packer_types.h` | **NEW** — packer internal types | PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition in `pack::detail::` |
| `tests/packer_standalone_compile_test.cpp` | **NEW** — compile-only | `#include "pack/packer.h"` only — verifies no transitive pack_service.h dependency |
| Consumer files (8 files) | `#include` adjustment | Replace `#include "pack/pack_service.h"` with `#include "pack/pack_types.h"` where only value types are needed |

---

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| Test framework | Catch2 |
| Assertions | 909 (215 test cases) |
