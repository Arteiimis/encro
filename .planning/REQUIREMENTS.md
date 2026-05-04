# Requirements: encro — v1.5 Pack下沉收尾

**Defined:** 2026-05-04
**Core Value:** Progress visibility — compact single-bar progress by default

## v1.5 Requirements

Requirements for eliminating picture_process.cpp's pack internal dependencies and extending PackRequest API.

### Naming Strategy (SINK-01)

- [x] **SINK-01**: `NamingStrategy {Flat, FlatWithForce, Keep}` enum in `pack.h` replaces `OutputLayout`+`forceConflictHandling` boolean pair. `NamingConfig` extended with `namingStrategy` field. Internal dispatch uses single-switch. Consumers translate at call site. `AppConfig` fields preserved for CLI parsing.

### Grouping + Summary (SINK-02)

- [x] **SINK-02**: `GroupingStrategy` enum + config and `SummaryConfig` struct (with explicit `isSummary` flag, not prefix convention) added to `PackRequest`. `buildMediaPackPlan` internalizes two-layer logical partitioning behind grouping strategy. Summary entries guaranteed first via structural flag.

### Picture Leak Elimination (SINK-03)

- [x] **SINK-03**: `picture_process.cpp` constructs `PackRequest` with new fields instead of constructing `PackPlan` directly. All 5 internal includes removed (`packer_types.h`, `packer.h`, `pack_internal.h`). Golden zip entry name tests pass with byte-identical output.

### PackPlan Internalization (SINK-04)

- [x] **SINK-04**: `PackPlan` moved from `pack_types.h` to internal header — consumers cannot include it. `static_assert(is_aggregate_v)` removed. All tests pass with zero behavioral change.

## v2 Requirements

(None — all v1.5 features included in scope)

## Out of Scope

| Feature | Reason |
|---------|--------|
| `entryNameForFile` callback deprecation | Still useful for consumer-specific overrides; independent concern |
| New external libraries or build changes | Zero needed — all abstractions use existing C++26 types |
| Picture E2E CLI verification (requires test media + FFmpeg) | Deferred from v1.3, unchanged |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| SINK-01 | Phase 15 | Complete |
| SINK-02 | Phase 16 | Complete |
| SINK-03 | Phase 17 | Complete |
| SINK-04 | Phase 18 | Complete |

**Coverage:**
- v1.5 requirements: 4 total
- Mapped to phases: 4 ✓
- Unmapped: 0

---
*Requirements defined: 2026-05-04*
*Last updated: 2026-05-04 after initial definition*
