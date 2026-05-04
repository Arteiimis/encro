# Phase 18: PackPlan Pure Internalization — Research

**Date:** 2026-05-04
**Status:** Complete

## 1. Current Architecture

### Type Locations (pre-refactor)

| Type | Header | Visibility |
|------|--------|------------|
| `PackPlan` | `pack_types.h` | Public (via `pack.h` → `pack_types.h`) |
| `PackFileEntry` | `pack_types.h` | Public (via `pack.h` → `pack_types.h`) |
| `PackEntryInput` | `pack_types.h` | Public |
| `FileOrdinalRange` | `pack_types.h` | Public |
| `PackProgressCallbacks` | `pack_types.h` | Public |
| `PackRunResult` | `pack.h` | Public |
| `PackMode` | `pack.h` | Public |
| `GroupingStrategy` | `pack.h` | Public |
| `NamingStrategy` | `pack.h` | Public |
| `NamingConfig` | `pack.h` | Public |
| `SummaryConfig` | `pack.h` | Public (uses `std::vector<PackFileEntry>`) |
| `PackRequest` | `pack.h` | Public |

### Include Chain (pre-refactor)

```
pack.h ──→ pack_types.h ──→ PackPlan + PackFileEntry + PackEntryInput + ...
  ↑            ↑
  │            ├── pack_internal.h ──→ pack::internal utils (resolveZipNameForIndex, selectPackPlanIndexes)
  │            ├── packer_types.h ──→ pack::detail (PackEntryPartition uses PackFileEntry)
  ├── packer.h ──→ includes pack.h + pack_types.h + packer_types.h
  └── pack_service.h ──→ includes pack.h + pack_types.h + packer.h
```

**Consumers of `pack.h` (public header):**
- `src/picture/picture_process.cpp` — uses `PackFileEntry`, `PackEntryInput`, `PackRequest`
- `src/video/video_process.cpp` — uses `PackRequest`
- `src/app/pipeline.cpp` — uses `PackRequest`

**No external consumer uses `PackPlan` directly.** Confirmed by audit — `PictureProcess.cpp` has 0 PackPlan references.

## 2. Key Finding: PackFileEntry Internalization Conflict

### Problem

CONTEXT.md D-01 proposes moving **both** PackPlan and PackFileEntry to internal header. However:

- `SummaryConfig` (defined in `pack.h`, public API) has field `std::vector<PackFileEntry> entries;`
- `std::vector<T>` requires complete type `T` at point of definition
- Forward-declaring `PackFileEntry` in `pack.h` is insufficient for `std::vector<PackFileEntry>`
- Moving PackFileEntry to internal header would require either:
  - Changing `SummaryConfig::entries` to a type-erased container (API break)
  - Moving `SummaryConfig` internal too (API break, contradicts D-02)

### Public Consumers of PackFileEntry

| Consumer | Status After Phase 17 |
|----------|----------------------|
| `SummaryConfig::entries` (`pack.h`) | Always public — part of PackRequest API |
| `picture_process.cpp` (6 refs) | **Removed** after Phase 17 (SINK-03) |
| `PackEntryPartition` (`packer_types.h`) | Internal (`pack::detail` namespace) |

### Recommendation

**Only internalize PackPlan. Keep PackFileEntry in `pack_types.h`.** This resolves the conflict cleanly:

- SINK-04 only requires PackPlan internalization ("PackPlan moved from pack_types.h to internal header")
- D-01's PackFileEntry addition is an aspirational optimization, but the compile-time boundary only needs PackPlan hidden
- After Phase 17, the only external PackFileEntry consumer is `SummaryConfig` (legitimate public API usage)
- Zero behavioral change — no API breakage

**Deviation from CONTEXT.md D-01:** Updated recommendation to internalize PackPlan only, keeping PackFileEntry public.

## 3. PackPlan References — Full Audit

### Headers (pre-refactor)

| File | Lines | Context |
|------|-------|---------|
| `pack.h:96` | 1 | `auto execute(PackPlan const& plan, ...)` — **public declaration, MUST be removed** |
| `pack_types.h:48-57` | 1 | `struct PackPlan { ... }` — definition, move to internal |
| `pack_types.h:59-62` | 1 | `static_assert(is_aggregate_v<PackPlan>)` — move or remove |
| `pack_service.h:22,24,42,43` | 4 | `PackPlan const&` params — internal, stays |
| `packer.h:80-88` | 1 | `buildDirectoryPackPlan() -> eh::Result<PackPlan>` — internal, stays |
| `pack_internal.h:18-21` | 4 | `resolveZipNameForIndex`, `selectPackPlanIndexes` etc — internal, stays |

### Sources (pre-refactor)

| File | Lines | Context |
|------|-------|---------|
| `pack.cpp` | 15 | execute(), buildMediaPackPlan(), runNonResumable(), runResumable() |
| `pack_service.cpp` | 12 | packGroupsCompact/Full, resolveZipNameForIndex, selectPackPlanIndexes |
| `packer.cpp` | 3 | buildDirectoryPackPlan, PackPlan construction |

### Tests (pre-refactor)

| File | Lines | Context |
|------|-------|---------|
| `pack_service_tests.cpp` | 19 | Construct PackPlan, test selectPackPlanIndexes, test runPackPlan |
| `pack_service_mock_tests.cpp` | 5 | Construct PackPlan with designated initializers |

**Total: ~42 PackPlan references in `src/` + ~24 in `tests/`.** All in `src/pack/` or test files. Zero in external consumers.

## 4. `execute(PackPlan const&)` — Call Sites

Found in `pack.h:96` (declaration) and `pack.cpp:381` (definition). **Only called from `pack.cpp:432`** — internal dispatch from `execute(PackRequest)`:

```cpp
// pack.cpp:432
auto const result = execute(plan, request.jobState);
```

This is an internal detail — `execute(PackRequest)` builds a PackPlan and passes it to the internal `execute(PackPlan)`. No external caller exists. **Safe to remove from public header.**

## 5. After-Refactor Architecture

### Include Chain (post-refactor)

```
pack.h ──→ pack_types.h ──→ PackFileEntry, PackEntryInput, FileOrdinalRange, PackProgressCallbacks
  │                              (NO PackPlan)
  ├── packer.h ──→ pack.h + pack_plan_internal.h + packer_types.h
  ├── pack_service.h ──→ pack.h + pack_plan_internal.h + packer.h
  └── pack_plan_internal.h ──→ pack_types.h + PackPlan struct definition
       ↑
       ├── pack_internal.h ──→ pack_plan_internal.h (was: pack_types.h)
       ├── packer.h
       ├── pack_service.h
       └── pack.cpp (.cpp only, for execute(PackPlan) + buildMediaPackPlan)
```

### Type Relocation

| Type | Old Location | New Location |
|------|-------------|--------------|
| `PackPlan` | `pack_types.h` | `pack_plan_internal.h` (NEW) |
| `static_assert(is_aggregate_v<PackPlan>)` | `pack_types.h:59-62` | Removed (internal-only type doesn't need guard) |
| `PackFileEntry` | `pack_types.h` | Stays in `pack_types.h` |
| `execute(PackPlan const&)` | `pack.h:96` (public) | `pack_plan_internal.h` (internal) |
| All other types | Unchanged | Unchanged |

## 6. Compile-Time Test Approaches

### Approach A: SFINAE Type Detection (Recommended)

```cpp
#include "pack/pack.h"
#include <type_traits>

template<typename, typename = void>
struct has_PackPlan_in_pack_ns : std::false_type {};

template<typename T>
struct has_PackPlan_in_pack_ns<T, std::void_t<decltype(sizeof(T::PackPlan))>>
  : std::true_type {};

static_assert(
  !has_PackPlan_in_pack_ns<pack>::value,
  "FAIL: pack::PackPlan is still visible from pack.h"
);
// If this compiles, PackPlan is properly hidden.

int main() { return 0; }
```

**Pros:** Self-contained, no build-system changes, standard C++17, clear pass/fail
**Cons:** `sizeof(typename T::PackPlan)` requires PackPlan to be a complete type in `pack` namespace — if it's only forward-declared (not that we plan to), this still triggers. Option: use `decltype(&T::PackPlan)` to detect even incomplete types.

### Approach B: Negative Compilation Test

Separate `.cpp` file that `#include "pack/pack.h"` then attempts `pack::PackPlan p;`. Marked as expected-to-fail via CMake `set_tests_properties(... WILL_FAIL TRUE)`.

**Pros:** Most "realistic" test — actually tries to use the type
**Cons:** Requires CMake configuration; harder to distinguish "expected fail" from "build system broke"

### Recommendation: Use Approach A (SFINAE) as primary verification. It's a normal test that compiles and passes, providing a clear compile-time assertion.

## 7. Task Breakdown Preview

Based on research, the work decomposes into:

1. **Create `pack_plan_internal.h`** — Move PackPlan struct definition + `execute(PackPlan const&)` declaration
2. **Remove PackPlan from `pack_types.h`** — Delete the struct and static_assert
3. **Remove `execute(PackPlan const&)` from `pack.h`**
4. **Update includes**: `pack_internal.h`, `packer.h`, `pack_service.h`, `pack.cpp`, `pack_service.cpp`  → include `pack_plan_internal.h`
5. **Update tests**: test files include `pack_plan_internal.h`
6. **Compile-time verification test**: SFINAE-based negative test
7. **Regression**: Full test suite pass with zero behavioral change

## 8. Risk Analysis

| Risk | Severity | Mitigation |
|------|----------|------------|
| Missing include in internal files causes ODR issues | Low | grep-based audit; compiler catches immediately |
| Test files fail to find PackPlan after relocation | Low | Update test includes to `pack_plan_internal.h` |
| PictureProcess still uses PackFileEntry after Phase 17 | Low | Phase 17 removes these; Phase 18 verifies |
| SummaryConfig PackFileEntry dependency blocks split | **None** | Keeping PackFileEntry public resolves this |
| Resumable job state deserialization depends on PackPlan layout | None | PackPlan stays aggregate; layout unchanged |

## 9. Decisions Locked

- **DR-01:** Only PackPlan (not PackFileEntry) moves to `pack_plan_internal.h`
- **DR-02:** `execute(PackPlan const&)` declaration moves to `pack_plan_internal.h` (internal API)
- **DR-03:** `static_assert(is_aggregate_v<PackPlan>)` removed (no longer needed for internal-only type)
- **DR-04:** SFINAE-based compile-time test verifies PackPlan is unreachable from `pack.h`
- **DR-05:** `pack_types.h` retains: PackFileEntry, PackEntryInput, FileOrdinalRange, PackProgressCallbacks

## Validation Architecture

### Dimension 1: Compile Boundary — PackPlan not reachable from `pack.h`
### Dimension 2: Compile Boundary — `#include "pack/pack.h"` succeeds (no broken includes)
### Dimension 3: Internal Correctness — all `src/pack/` files find PackPlan via `pack_plan_internal.h`
### Dimension 4: Test Correctness — all tests compile and pass
### Dimension 5: Type Layout — PackPlan unchanged (aggregate, same fields, same memory layout)
### Dimension 6: API Integrity — `execute(PackRequest)` public API unchanged
### Dimension 7: No Behavioral Change — all existing test assertions pass identically
### Dimension 8: Negative Compile Test — SFINAE test asserts PackPlan absent from public namespace

---

*Research completed: 2026-05-04*
