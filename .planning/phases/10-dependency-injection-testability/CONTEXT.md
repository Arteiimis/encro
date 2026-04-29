# Phase 10 Context: Dependency Injection & Testability

**Date:** 2026-04-29
**Source:** Discussion phase (gsd-discuss-phase 10)
**Status:** Ready for planning

---

## Phase Overview

Introduce `IPacker` abstract interface, implement `MockPacker` test double, migrate `PackService` constructor from `Packer&` to `std::unique_ptr<IPacker>`, add unit tests without real zip I/O. All 909 assertions must pass — additive testing only.

---

## Phase 9 Inheritance

Phase 9 delivered:
- `class PackService final` with `explicit PackService(Packer&)` constructor
- `class Packer final` — all zip I/O + grouping methods
- `PackProgressCallbacks` sub-struct on PackPlan
- `pack_facade.h` — 21+ `[[deprecated]]` static wrappers
- 909 assertions pass, zero regressions

---

## Decisions Resolved

### D-01: IPacker Interface Granularity
**Verdict:** Minimalist — only methods PackService actually calls.

```cpp
class IPacker {
public:
    virtual ~IPacker() = default;

    // Called by PackService (4 call sites)
    virtual eh::Result<pack::PackRunResult> packFilesToZip(
        const std::vector<pack::PackFileEntry>& entries,
        const std::string& outputPath,
        const std::chrono::seconds& timeout,
        const pack::PackProgressCallbacks& callbacks) = 0;

    // Called by PackService (2 call sites)
    virtual eh::Result<pack::PackPlan> buildDirectoryPackPlan(
        const std::filesystem::path& directory) = 0;
};
```

Rationale: Grouping methods (`groupPackFiles`, `groupPackEntriesWithSubparts`) are called by consumer code directly, not through PackService. They're pure computation (no I/O) and don't need mocking. Only `packFilesToZip` + `buildDirectoryPackPlan` flow through PackService's dependency.

Virtual dispatch at archive granularity — never inside per-file loops. Zero hot-path impact per PITFALLS.md.

### D-02: ZipWriter Placement
**Verdict:** Internal to Packer — private helper struct inside `packer.cpp`.

Rationale: `ZipWriter` wraps `libzippp::ZipArchive` lifecycle (open → addFile loop → close). It's an implementation detail of Packer, not a DI boundary. Exposing through IPacker adds interface complexity without test value — MockPacker doesn't create zip files at all.

### D-03: MockPacker Design
**Verdict:** Capture-recording mock — stores method arguments in vectors, returns success.

```cpp
class MockPacker final : public IPacker {
public:
    // Capture recording
    struct PackFilesToZipCall {
        std::vector<pack::PackFileEntry> entries;
        std::string outputPath;
        std::chrono::seconds timeout;
        pack::PackProgressCallbacks callbacks;
    };
    struct BuildPlanCall {
        std::filesystem::path directory;
    };

    std::vector<PackFilesToZipCall> packFilesToZipCalls;
    std::vector<BuildPlanCall> buildPlanCalls;

    // Returns success by default; tests assert on capture state
    eh::Result<pack::PackRunResult> packFilesToZip(...) override { /* record + return success */ }
    eh::Result<pack::PackPlan> buildDirectoryPackPlan(...) override { /* record + return empty plan */ }
};
```

Rationale: Follows existing codebase test patterns (manual recording via vectors, straightforward Arrange-Act-Assert). No mock framework. Tests verify "PackService called packFilesToZip with expected entries" by inspecting the capture vectors.

### D-04: Constructor Migration
**Verdict:** One-step migration in Phase 10.

Current (Phase 9): `explicit PackService(Packer& packer)`
Target (Phase 10): `explicit PackService(std::unique_ptr<IPacker> packer)`

Migration plan:
1. Change PackService constructor signature
2. Update `pack_facade.h` — static wrapper allocates `std::make_unique<pack::Packer>()`
3. Update `pack_service_tests.cpp` — create `unique_ptr<Packer>` for existing tests
4. Consumer tests that create PackService instances (if any) — update similarly

Rationale: Phase 9 D-03 anticipated this: "changing from Packer& to unique_ptr<IPacker> is a one-line swap." Facade isolates most consumers from the change.

### D-05: Test Strategy
**Verdict:** Additive — new unit tests alongside preserved integration tests.

- **New:** `tests/pack_service_mock_tests.cpp` — 100% MockPacker, tests PackService orchestration logic (fast, no filesystem)
- **Preserved:** `tests/pack_service_tests.cpp` — real Packer + TempDir, tests integration with libzippp (safety net)
- **Preserved:** `tests/packer_tests.cpp` — real Packer, tests zip I/O and grouping

Rationale: Two complementary test layers. Mock tests give fast feedback on orchestration; integration tests catch real filesystem/zip regressions. No coverage loss risk.

---

## Requirements

| ID | Requirement | Status |
|----|-------------|--------|
| DI-01 | IPacker interface — archive granularity, ~5 virtual functions, no hot-path dispatch | Pending |
| DI-02 | Packer implements IPacker; MockPacker (capture-recording) implements IPacker | Pending |
| DI-03 | PackService constructor-injected with `std::unique_ptr<IPacker>` | Pending |
| DI-04 | ZipWriter RAII wrapper — private helper inside packer.cpp | Pending |
| DI-05 | `pack_service_mock_tests.cpp` — MockPacker unit tests, no real zip files | Pending |
| DI-06 | 909 assertions pass, zero regressions | Pending |

---

## Success Criteria

1. IPacker abstract interface with ~5 pure virtual methods at archive granularity
2. Packer marked `final` implementing IPacker; MockPacker recording all calls
3. PackService takes `std::unique_ptr<IPacker>` in constructor
4. `ZipWriter` RAII struct in `packer.cpp` (private) managing `libzippp::ZipArchive` lifecycle
5. `pack_service_mock_tests.cpp` — PackService orchestration tested without real zip I/O
6. All existing tests preserved unchanged
7. 909 assertions pass (existing) + new MockPacker assertions pass
8. `pack_facade.h` updated to construct `Packer` via `make_unique`

---

## Implementation Constraints

| Constraint | Source | Detail |
|-----------|--------|--------|
| Minimal interface | D-01 | IPacker only exposes what PackService needs; grouping stays on Packer directly |
| No virtual in hot path | PITFALLS.md | Virtual dispatch at archive level, never per-file |
| Additive testing | D-05 | Existing tests preserved; new mock tests added |
| Clean inheritance | PITFALLS.md | Max depth 1: IPacker → Packer/MockPacker; no deeper |
| Zero behavioral change | Baseline | All 909 existing assertions unchanged |
| Facade updated | D-04 | Facade wraps new constructor API |

---

## Key Files

| File | Role | Action |
|------|------|--------|
| `src/pack/ipacker.h` | **NEW** | IPacker abstract interface (~5 pure virtual methods) |
| `src/pack/packer.h` | **MODIFIED** | `class Packer final : public IPacker` |
| `src/pack/packer.cpp` | **MODIFIED** | ZipWriter RAII struct (private, in .cpp); override keywords on IPacker methods |
| `src/pack/pack_service.h` | **MODIFIED** | Constructor: `PackService(std::unique_ptr<IPacker>)` |
| `src/pack/pack_service.cpp` | **MODIFIED** | `packer_->packFilesToZip(...)` calls through IPacker |
| `src/pack/pack_facade.h` | **MODIFIED** | Static wrapper: `auto packer = std::make_unique<pack::Packer>()` |
| `tests/pack_service_mock_tests.cpp` | **NEW** | MockPacker unit tests |
| `tests/packer.h` (test helper) | **NEW** | MockPacker class definition |

---

## Environment

| Component | Version |
|-----------|---------|
| OS | Windows 11 |
| Compiler | clang-cl (C++26) |
| Build tool | xmake |
| Test framework | Catch2 |
| Assertions | 909 (215 test cases) + new MockPacker assertions |
