# Phase 14: IPacker 抽象层移除 — Research

**Performed:** 2026-05-01 (backfill from prior exploration)
**Status:** Complete

## Research Question

Is the `IPacker` abstract interface introduced in v1.3 necessary, or is it over-abstraction that should be removed?

## Key Findings

| Dimension | Finding |
|-----------|---------|
| Production implementations | 1 (`Packer final`), no other implementations exist or planned |
| Production consumers of IPacker interface | 1 (`PackService`), injected via `unique_ptr<IPacker>` |
| Production polymorphism | Zero — all 7 construction sites used `make_unique<Packer>()` |
| Mock consumer | 1 (`MockPacker` in `pack_service_mock_tests.cpp`, 10 tests) |
| Future backends | None planned (tar/7z not in REQUIREMENTS.md) |
| Virtual dispatch cost | Archive granularity (not hot path), but still unnecessary indirection |
| Signature maintenance | 3 method signatures synchronized across 3 files (ipacker.h, packer.h, packer_mock.h) |
| Alternative test coverage | `pack_service_tests.cpp` already tested same orchestration with real `Packer` + `TempDir` |

## Conclusion

**Remove IPacker.** The abstract layer exists solely to serve MockPacker — a test-only consumer. This is the "abstraction for testing" anti-pattern. The 10 mock tests can be rewritten as integration tests using real `Packer` + `TempDir`, matching the existing pattern in `pack_service_tests.cpp`.

## Detailed Analysis

See: `.planning/notes/remove-ipacker-abstraction.md` for the full decision rationale captured during Phase 13 exploration.

---
*Research documented: 2026-05-01*
