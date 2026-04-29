# Phase 10 Research: Dependency Injection & Testability

**Date:** 2026-04-30
**Status:** Complete

---

## 1. IPacker Virtual Method Signatures

### Audit: PackService → Packer Call Sites

PackService has 4 call sites to `packFilesToZip` and 2 to `buildDirectoryPackPlan`:

| # | File:Line | Method Called | Overload |
|---|-----------|---------------|----------|
| 1 | `pack_service.cpp:198-231` | `packer_.packFilesToZip(entries, zipPath, callback, &finalizingCount)` | Compact (4-param) |
| 2 | `pack_service.cpp:317-318` | `packer_.packFilesToZip(entries, zipPath, progressCtx, label)` | Full-progress (4-param) |
| 3 | `pack_service.cpp:509` | `packer_.buildDirectoryPackPlan(dirPath, zipFileDir, maxGroupSize, recursive, forceNameConflictHandling, maxParallelJobs)` | 6-param |
| 4 | `pack_service.cpp:530` | `packer_.buildDirectoryPackPlan(dirPath, zipOutputDir, kDefaultMaxArchiveGroupSize, true, ctx.config.forceNameConflictHandling, ctx.config.maxParallelJobs, excludedPath)` | 7-param |

### Decision: IPacker Unified Signatures

Following CONTEXT.md D-01, the IPacker exposes **2 pure virtual methods** that unify the overloads:

```cpp
class IPacker {
public:
    virtual ~IPacker() = default;

    // Unifies both compact and full-progress call sites
    // The Packer implementation internally selects behavior based on callbacks presence
    virtual eh::Result<pack::PackRunResult> packFilesToZip(
        const std::vector<pack::PackFileEntry>& entries,
        const std::string& outputPath,
        const std::chrono::seconds& timeout,
        const pack::PackProgressCallbacks& callbacks) = 0;

    // Unifies both buildDirectoryPackPlan call sites
    virtual eh::Result<pack::PackPlan> buildDirectoryPackPlan(
        const std::filesystem::path& directory) = 0;
};
```

**Actually — revised after code audit:**

The two `packFilesToZip` call sites use different 3rd/4th parameters:
- Compact: `(callback, &finalizingCount)` → `PackEntryProgressCallback` + `atomic<size_t>*`
- Full: `(progressCtx&, string_view)` → `ProgressContext&` + `string_view`

These are fundamentally different progress-reporting contracts. The IPacker interface must accommodate both. **Simplest viable approach**: keep the original Packer overloads with their exact signatures on the virtual interface, OR create a higher-level abstraction.

**Revised decision — Minimal Viable Approach:**

The Packer already has 3 `packFilesToZip` overloads. PackService only uses the 2 that take `PackFileEntry` vectors. The IPacker should expose these 2 methods:

```cpp
class IPacker {
public:
    virtual ~IPacker() = default;

    // Called by packGroupsFull (line 318)
    virtual eh::Result<void> packFilesToZip(
        const std::vector<pack::PackFileEntry>& entries,
        const std::filesystem::path& zipFilePath,
        progress::ProgressContext& progressCtx,
        std::string_view progressText) = 0;

    // Called by packGroupsCompact (line 198)
    virtual eh::Result<void> packFilesToZip(
        const std::vector<pack::PackFileEntry>& entries,
        const std::filesystem::path& zipFilePath,
        pack::detail::PackEntryProgressCallback onEntryPacked,
        std::atomic<std::size_t>* finalizingCount) = 0;

    // Called by packAllFilesInDirectory (line 509) and runDirectoryPackWorkflow (line 530)
    virtual eh::Result<pack::PackPlan> buildDirectoryPackPlan(
        const std::filesystem::path& dirPath,
        const std::filesystem::path& zipFileDir,
        std::uintmax_t maxGroupSize,
        bool recursive,
        bool forceNameConflictHandling,
        std::optional<std::size_t> maxParallelJobs,
        std::optional<std::filesystem::path> excludedPath = std::nullopt) = 0;
};
```

**This is 3 virtual methods (not ~5 as originally estimated).** The original D-01 estimate of "~5" was conservative. Actual count: **3**.

**Rationale for keeping current signatures:**
- Zero refactoring of PackService call sites — just change `packer_.` → `packer_->`
- MockPacker simply records arguments and returns success
- Existing Packer signatures are well-tested — no regression risk from interface changes

### Virtual Dispatch Impact

- `packFilesToZip`: Called once per zip archive (not per file). Typical workload: 1-3 archives. **Zero hot-path impact.**
- `buildDirectoryPackPlan`: Called once per pack workflow. **Zero hot-path impact.**

---

## 2. ZipWriter RAII Wrapper Design

Current pattern in `packer.cpp` (lines 372-412, 434-455):
```cpp
auto zip = libzippp::ZipArchive(zipFilePath.string());
zip.open(libzippp::ZipArchive::New);
// ... addFile loop ...
zip.close();
```

**Design:**

```cpp
// Private helper struct inside packer.cpp
struct ZipWriter {
    libzippp::ZipArchive zip;
    bool opened = false;

    explicit ZipWriter(const std::string& path)
        : zip(path) {}

    auto open() -> void {
        zip.open(libzippp::ZipArchive::New);
        opened = true;
    }

    ~ZipWriter() {
        if (opened) {
            try { zip.close(); }
            catch (...) { /* log but don't throw from destructor */ }
        }
    }

    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
};
```

Usage replaces:
```cpp
auto zip = libzippp::ZipArchive(path);
zip.open(libzippp::ZipArchive::New);
// ...
zip.close();
```
With:
```cpp
auto zip = ZipWriter(path);
zip.open();
// ... same addFile loop ...
// zip.close() called automatically by destructor
```

**Benefit:** Exception-safe close (if exception during addFile loop, zip.close() still called). Matches RAII principle per CONTEXT.md D-02.

**Note:** The `runFinalizingSpinner` logic (spinner thread + `finalizing` atomic) stays outside ZipWriter — it's progress reporting, not resource management.

---

## 3. MockPacker Implementation Patterns

### Pattern: Manual Capture Recording (No Framework)

Per CONTEXT.md D-03 and existing project convention (no mock frameworks):

```cpp
// tests/packer_mock.h
#pragma once
#include "src/pack/ipacker.h"

namespace pack::test {

class MockPacker final : public IPacker {
public:
    struct PackFilesToZipCall {
        std::vector<PackFileEntry> entries;
        std::filesystem::path zipFilePath;
        progress::ProgressContext* progressCtx = nullptr;
        std::string progressText;
        pack::detail::PackEntryProgressCallback onEntryPacked;
        std::atomic<std::size_t>* finalizingCount = nullptr;
        bool isCompact = false;  // true = compact overload, false = full-progress overload
    };

    struct BuildPlanCall {
        std::filesystem::path dirPath;
        std::filesystem::path zipFileDir;
        std::uintmax_t maxGroupSize = 0;
        bool recursive = false;
        bool forceNameConflictHandling = false;
        std::optional<std::size_t> maxParallelJobs;
        std::optional<std::filesystem::path> excludedPath;
    };

    std::vector<PackFilesToZipCall> packFilesToZipCalls;
    std::vector<BuildPlanCall> buildPlanCalls;

    // Configurable return values
    eh::Result<void> packFilesToZipResult = {};
    eh::Result<pack::PackPlan> buildPlanResult = pack::PackPlan{};

    // Full-progress overload
    eh::Result<void> packFilesToZip(
        const std::vector<PackFileEntry>& entries,
        const std::filesystem::path& zipFilePath,
        progress::ProgressContext& progressCtx,
        std::string_view progressText) override {
        packFilesToZipCalls.push_back({
            .entries = entries,
            .zipFilePath = zipFilePath,
            .progressCtx = &progressCtx,
            .progressText = std::string{progressText},
            .isCompact = false,
        });
        return packFilesToZipResult;
    }

    // Compact overload
    eh::Result<void> packFilesToZip(
        const std::vector<PackFileEntry>& entries,
        const std::filesystem::path& zipFilePath,
        pack::detail::PackEntryProgressCallback onEntryPacked,
        std::atomic<std::size_t>* finalizingCount) override {
        packFilesToZipCalls.push_back({
            .entries = entries,
            .zipFilePath = zipFilePath,
            .onEntryPacked = onEntryPacked,
            .finalizingCount = finalizingCount,
            .isCompact = true,
        });
        return packFilesToZipResult;
    }

    eh::Result<pack::PackPlan> buildDirectoryPackPlan(
        const std::filesystem::path& dirPath,
        const std::filesystem::path& zipFileDir,
        std::uintmax_t maxGroupSize,
        bool recursive,
        bool forceNameConflictHandling,
        std::optional<std::size_t> maxParallelJobs,
        std::optional<std::filesystem::path> excludedPath) override {
        buildPlanCalls.push_back({
            .dirPath = dirPath,
            .zipFileDir = zipFileDir,
            .maxGroupSize = maxGroupSize,
            .recursive = recursive,
            .forceNameConflictHandling = forceNameConflictHandling,
            .maxParallelJobs = maxParallelJobs,
            .excludedPath = excludedPath,
        });
        return buildPlanResult;
    }

    void reset() {
        packFilesToZipCalls.clear();
        buildPlanCalls.clear();
    }
};

}  // namespace pack::test
```

### Test Assertions Pattern

```cpp
// Verify PackService called packFilesToZip with expected entries
REQUIRE(mock.packFilesToZipCalls.size() == 1);
CHECK(mock.packFilesToZipCalls[0].entries == expectedEntries);
CHECK(mock.packFilesToZipCalls[0].zipFilePath == expectedPath);

// Verify error propagation
mock.packFilesToZipResult = eh::makeError("simulated failure");
auto result = service.packGroups(plan);
REQUIRE_FALSE(result);
CHECK(result.error().find("simulated failure") != std::string::npos);
```

---

## 4. PackService Constructor Migration Strategy

### Before (Phase 9):
```cpp
// pack_service.h
class PackService final {
public:
    explicit PackService(Packer& packer);
private:
    Packer& packer_;  // reference member
};
```

### After (Phase 10):
```cpp
// pack_service.h
#include <memory>
class IPacker;  // forward declaration

class PackService final {
public:
    explicit PackService(std::unique_ptr<IPacker> packer);
private:
    std::unique_ptr<IPacker> packer_;  // owned pointer
};
```

### Call Site Changes Required:

1. **`pack_service.cpp`**: `packer_.` → `packer_->` (all 6 call sites)
2. **`pack_facade.h`**: Static instances change from:
   ```cpp
   static pack::Packer packer;
   static pack::PackService service(packer);
   ```
   To:
   ```cpp
   static pack::PackService service(std::make_unique<pack::Packer>());
   ```
   (Packer is default-constructible, no extra state needed)

3. **`pack_service_tests.cpp`**: Test setup changes from:
   ```cpp
   pack::Packer testPacker;
   pack::PackService testService(testPacker);
   ```
   To:
   ```cpp
   auto testPacker = std::make_unique<pack::Packer>();
   pack::PackService testService(std::move(testPacker));
   ```

4. **Consumer code**: No changes needed — all consumers go through `pack_facade.h` or will be migrated in Phase 11.

---

## 5. Test Strategy

### New Test File: `tests/pack_service_mock_tests.cpp`

Test cases:

| # | Test Case | What It Verifies |
|---|-----------|-----------------|
| 1 | `packGroups calls IPacker::packFilesToZip for each group` | Correct delegation from PackService to IPacker |
| 2 | `packGroups passes correct zip paths derived from plan` | Zip name resolution + output path joining works |
| 3 | `packGroups propagates IPacker errors` | Error handling when MockPacker returns failure |
| 4 | `packGroups returns zipped file paths on success` | Return value assembly from IPacker results |
| 5 | `packGroups empty plan returns empty result` | Edge case: no groups |
| 6 | `packGroups compact mode uses compact overload` | Verified by checking `mock.packFilesToZipCalls[0].isCompact == true` |
| 7 | `packGroups full mode uses full-progress overload` | Verified by checking `mock.packFilesToZipCalls[0].isCompact == false` |
| 8 | `packAllFilesInDirectory delegates to buildDirectoryPackPlan then packGroups` | Full workflow test with mock |
| 9 | `buildDirectoryPackPlan receives correct parameters from packAllFilesInDirectory` | Parameter forwarding test |
| 10 | `runPackPlan skips when job state marks stage completed` | Job state integration with mock packer |

**Total: ~10 new test cases, estimated 30-40 new assertions.**

---

## 6. File Dependency Graph

```
ipacker.h  (NEW - no dependencies)
    ↑
    ├── packer.h (MODIFIED - : public IPacker)
    │       ↑
    │       └── packer.cpp (MODIFIED - ZipWriter RAII, override keywords)
    │
    ├── packer_mock.h (NEW - : public IPacker, tests/)
    │
    └── pack_service.h (MODIFIED - unique_ptr<IPacker>)
            ↑
            ├── pack_service.cpp (MODIFIED - packer_-> calls)
            ├── pack_facade.h (MODIFIED - make_unique<Packer>())
            ├── pack_service_tests.cpp (MODIFIED - unique_ptr setup)
            └── pack_service_mock_tests.cpp (NEW - MockPacker based)
```

---

## 7. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Packer method hiding | Medium | Explicit `using IPacker::packFilesToZip` declarations to expose both virtual + non-virtual overloads |
| Facade static init order | Low | Packer is default-constructible (no external dependencies); `make_unique<Packer>()` is safe in static context |
| Unique_ptr move semantics in tests | Low | Test setup explicitly moves; reset for each test case |
| Build break from header changes | Medium | Incremental waves; compile after each plan before proceeding |

---

## 8. Build Verification

Command: `xmake build` — must compile with zero errors before proceeding to next wave.

Test command: `xmake run tests` — Catch2 test runner, 909 existing + ~30 new assertions.

---

## Research COMPLETE

All technical unknowns resolved. Ready for planning.
