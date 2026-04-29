# Stack Research — C++ OO Encapsulation Patterns

**Domain:** C++ OO refactoring — procedural/functional → encapsulated class design
**Researched:** 2026-04-29
**Confidence:** HIGH

## Executive Summary

This research identifies the C++ patterns, idioms, and anti-patterns specifically suited for incrementally refactoring the encro pack subsystem from public structs + free functions to encapsulated classes with private data, injectable dependencies, and mockable interfaces. All recommendations are backed by the C++ Core Guidelines (isocpp), validated against C++20/26 standard features, and targeted at the existing codebase's constraints: C++26/clang-cl/xmake/Catch2, 909 existing assertions that must not regress.

## Recommended Stack — Patterns & Techniques

### 1. Struct → Class Conversion: Incremental Privatization

**Core Guideline:** C.134 — Make non-const data members `private` if they participate in the object's invariant. Keep them `public` if they're just a data bundle with no internal invariants.

| Pattern | When to Apply | Complexity | Risk |
|---------|--------------|------------|------|
| **Invariant Audit First** | Before touching any struct | Low | None |
| **Stepwise Field Privatization** | One field at a time, keep backward compat | Low | Low |
| **Named Constructor / Factory** | When designated-initializer callers exist (`PackPlan{}`, `PackPlan{.groups=...}`) | Medium | Medium |
| **Accessor Pairs (const getter)** | Data that's read externally but should be read-only | Low | None |

**Rationale:** The codebase currently has a `static_assert(std::is_aggregate_v<pack::PackPlan>)` guarding designated-initializer usage. This is the highest-risk struct to convert. The safe approach: audit invariants per struct first. Many structs (like `PackGroupInput`, `PackEntryInput`, `PackGroupPartition`) are pure data bundles with no invariants — these should remain as public-data `struct`s per C.134. Only structs with behavioral invariants (state that must be maintained coherently) should become `class`es with `private` data.

**Step-by-step conversion pattern:**

```cpp
// BEFORE: public struct aggregate
struct PackPlan {
    std::vector<std::vector<PackFileEntry>> groups;
    fs::path outputDir;
    bool removeOnFailure = false;
    bool compact = true;
    // ... other public fields
};

// STEP 1: Add private data, keep public fields for compatible access (transitional)
class PackPlan {
public:
    // Backward-compatible accessors (temporary — remove after migration)
    const auto& groups() const { return groups_; }
    const auto& outputDir() const { return outputDir_; }
    // ... accessors for all fields

    // New behavior methods
    auto zipNameForIndex(std::size_t index) const -> std::string;
    void onGroupStart(std::size_t groupIndex) const;

private:
    std::vector<std::vector<PackFileEntry>> groups_;
    fs::path outputDir_;
    // ... private fields
    // Factory for construction (replaces designated initializers)
public:
    struct Config {
        std::vector<std::vector<PackFileEntry>> groups;
        fs::path outputDir;
        bool removeOnFailure = false;
        bool compact = true;
    };
    explicit PackPlan(Config cfg);
};
```

**Critical constraint:** `PackPlan` has a `static_assert(std::is_aggregate_v<pack::PackPlan>)`. Removing this requires removing ALL designated-initializer call sites first (at least 4 sites in tests and production code). This should be a dedicated phase-transition step.

### 2. Free Functions → Class Methods: Adapter + Gradual Migration

**Core Guideline:** C.4 — Make a function a member only if it needs direct access to the representation of a class.

| Pattern | Use Case | Risk |
|---------|----------|------|
| **Facade Delegate** | Free function that wraps a new method call | Low |
| **Method Extraction + Thin Wrapper** | Move implementation to method, keep free function as forwarder | Low |
| **Anonymous Namespace Internal Move** | Functions already in anonymous namespaces become `private` methods | Lowest |

**Rationale:** The codebase has free functions in anonymous namespaces inside `pack_service.cpp` and `packer.cpp`. These are already internal. The approach: classify which free functions genuinely need representation access (→ methods) vs. which are algorithmic utilities that should remain free (→ `static` or kept as file-scope helpers).

**Recommended migration pattern:**

```cpp
// BEFORE: free function in anonymous namespace
namespace pack {
namespace {
    auto countPackedFiles(std::vector<std::vector<PackFileEntry>> const& groups)
      -> std::size_t { /* ... */ }
} // namespace

// AFTER: becomes private static member (no instance access needed)
class PackService {
private:
    static auto countPackedFiles(
        std::vector<std::vector<PackFileEntry>> const& groups
    ) -> std::size_t;
};

// For functions that callers currently use directly:
// Keep a forwarding free function for backward compatibility

// BEFORE: caller writes: auto plan = pack::buildDirectoryPackPlan(...);
// AFTER: same call still works (delegates to class)
auto buildDirectoryPackPlan(
    fs::path const& dirPath, fs::path const& zipFileDir,
    std::uintmax_t maxGroupSize, bool recursive,
    bool forceNameConflictHandling,
    std::optional<std::size_t> maxParallelJobs,
    std::optional<fs::path> excludedPath
) -> eh::Result<pack::PackPlan> {
    return PackPlanBuilder::build(dirPath, zipFileDir, maxGroupSize,
        recursive, forceNameConflictHandling, maxParallelJobs, excludedPath);
}
```

**Priority classification for the encro codebase:**

| Functions | Action | Reason |
|-----------|--------|--------|
| `countPackedFiles` (anon ns) | → `private static` method | No state access needed; algorithm utility |
| `formatCompactPackingStatus` (anon ns) | → `private static` method | Pure formatting, no state |
| `packFilesToZip` overloads (public, packer.h) | → `Packer::packToZip` + forwarding wrappers | Core behavior; main refactoring target |
| `groupFilesBySize`, `groupPackFiles` (public) | → `Packer` static methods or keep as free | Algorithmic; don't need private data |
| `runPackPlan` (public) | → `PackService::run` (instance method) | Needs context; primary orchestrator |
| `packGroups` (public) | → `PackService::packGroups` (instance method) | Needs `PackPlan` state |

### 3. Dependency Injection for Testability

**Core Guideline:** I.23 — Keep the number of function arguments low. I.4 — Make interfaces precisely and strongly typed.

| Pattern | Use When | Example |
|---------|----------|---------|
| **Constructor Injection** (preferred) | Dependency required for object lifetime | `PackService(AppContext& ctx)` |
| **Setter/Property Injection** | Optional/reconfigurable dependency | `packer.setProgressCallback(cb)` |
| **Template Parameter (compile-time DI)** | Performance-critical; no virtual overhead | `template<typename ZipImpl> class Packer` |
| **Interface Pointer Injection** | Need to swap implementations for testing | `PackService(std::unique_ptr<IZipWriter>)` |

**For the encro codebase, the recommended hierarchy:**

```
AppContext (existing, stays as value bundle)
    ├── RuntimeContext (existing)
    │   └── jobState: shared_ptr<jobstate::Store>  ← already DI pattern!
    │
    └── (NEW) injectable dependencies
        ├── IFileSystem        ← mockable for tests
        ├── IZipWriter         ← mockable for pack tests
        └── IProcessRunner     ← mockable for FFmpeg tests
```

**The existing `jobstate::Store` is already a good example of constructor DI in this codebase:**

```cpp
class Store {
public:
    explicit Store(fs::path stateFilePath);  // ← Constructor DI
    // ... public methods, private data ...
};
```

**Recommended DI approach for PackService:**

```cpp
// Interface for mockable zip operations
class IZipWriter {
public:
    virtual ~IZipWriter() = default;
    virtual auto addFile(fs::path const& sourcePath,
                         std::string_view entryName) -> eh::Result<void> = 0;
    virtual auto finalize() -> eh::Result<void> = 0;
};

// Production implementation (wraps libzippp)
class LibzipppWriter : public IZipWriter { /* ... */ };

// Test mock
class MockZipWriter : public IZipWriter { /* ... */ };

// PackService with DI
class PackService {
public:
    // Constructor injection — production
    explicit PackService(appctx::AppContext& ctx);

    // Constructor injection — testable (packages IZipWriter via unique_ptr)
    PackService(appctx::AppContext& ctx,
                std::unique_ptr<IZipWriter> zipWriter);

    auto run(PackPlan const& plan) -> eh::Result<PackRunResult>;

private:
    appctx::AppContext& ctx_;
    std::unique_ptr<IZipWriter> zipWriter_;
};
```

**When NOT to use dependency injection:**
- For value types that are cheap to construct (e.g., `PackPlan`, `PackFileEntry` — these stay as structs)
- For things that already have no side effects (pure computation functions)
- For dependencies that are already abstracted via standard library types (`std::filesystem::path`)

### 4. Interface / Abstract Class Patterns for Mockable Dependencies

**Core Guideline:** C.121 — If a base class is used as an interface, make it a pure abstract class. I.25 — Prefer empty abstract classes as interfaces to class hierarchies.

**The canonical C++ interface pattern:**

```cpp
// Pure abstract interface — the C++ Core Guidelines way
class IZipWriter {
public:
    virtual ~IZipWriter() = default;  // ← CRITICAL: virtual destructor

    // Only pure virtual functions, no data members
    virtual auto open(fs::path const& zipPath) -> eh::Result<void> = 0;
    virtual auto addFile(fs::path const& src,
                         std::string_view entryName) -> eh::Result<void> = 0;
    virtual auto close() -> eh::Result<void> = 0;

    // Prevent copying (interfaces usually manage resources)
    IZipWriter(IZipWriter const&) = delete;
    IZipWriter& operator=(IZipWriter const&) = delete;

protected:
    IZipWriter() = default;  // Only derived classes construct
};
```

**NVI (Non-Virtual Interface) pattern — when you need pre/post conditions:**

```cpp
// Public non-virtual method enforces invariants
// Private virtual method is the customization point
class PackService {
public:
    auto run(PackPlan const& plan) -> eh::Result<PackRunResult> {
        // Pre-condition checks
        if (plan.groups.empty()) return PackRunResult{};

        // Delegate to virtual implementation
        auto result = runImpl(plan);

        // Post-condition checks
        return result;
    }

private:
    virtual auto runImpl(PackPlan const& plan) -> eh::Result<PackRunResult> = 0;
};
```

**Interface identification for the encro codebase:**

| External Dependency | Interface | Production Impl | Test Mock |
|--------------------|-----------|-----------------|-----------|
| libzippp (zip creation) | `IZipWriter` | `LibzipppWriter` | `MockZipWriter` |
| FFmpeg (process execution) | `IProcessRunner` | `FfmpegRunner` | `MockProcessRunner` |
| Filesystem (I/O) | `IFileSystem` | — use `std::filesystem` directly (already mockable via temp dirs) | `TempDir` (exists) |
| Progress reporting | `IProgressReporter` | `Progress::ProgressContext` adapter | `FakeProgressReporter` |

**Critical decision: How many interfaces?**

Start with ONE interface (`IZipWriter`) for the pack subsystem. The codebase's Catch2 tests already use `TempDir` and real filesystem for integration testing — this is working well. Only add interfaces where the existing pattern is insufficient. The 909 assertions already pass without mocks — don't over-mock.

### 5. Modern C++20/23/26 Features for Encapsulation

| Feature | C++ Version | Relevance to This Refactor | Recommendation |
|---------|------------|---------------------------|----------------|
| **Modules** | C++20 | TRUE encapsulation boundary — `module linkage` prevents leakage | **DEFER** — clang-cl module support for C++20 is maturing but not yet production-stable for MSVC ABI targets. The codebase uses `#pragma once` + header guards which work. Modules would be a separate milestone (v2.0+). |
| **Concepts** | C++20 | Constrain template-based DI; document interface requirements | **USE sparingly**. Define concepts for template-based DI: `concept ZipWriter = requires(T w, fs::path p) { { w.addFile(p, "") } -> std::same_as<eh::Result<void>>; };` |
| **`std::span`** | C++20 | Already used in codebase for view semantics | Continue using — great for read-only views of data |
| **Three-way comparison `<=>`** | C++20 | For value types that need ordering | Use `= default` on value structs like `PackFileEntry` |
| **`explicit(bool)`** | C++20 | Conditional explicit constructors | Niche — use when writing generic factory wrappers |
| **`consteval`** | C++20 | Compile-time validation of invariants | Use for compile-time constant validation |
| **Deducing `this`** | C++23 | Simplifies CRTP, removes const/non-const duplication | **LOW priority** — the current codebase doesn't use CRTP |
| **Contracts** | C++26 | Pre/post-condition checking on methods | **HIGHLY RECOMMENDED** when compiler support matures. The pack subsystem's methods naturally have contracts: `packGroups` requires non-empty groups, `addFile` requires valid source paths. Use `[[expects: ...]]` and `[[ensures: ...]]` once available. |
| **`std::expected`** | C++23 | Better error handling than `eh::Result` | **EVALUATE** — the codebase uses `eh::Result<void>` heavily. `std::expected` provides better ergonomics but would require touching all call sites. Keep `eh::Result` for this milestone. |

**What to use NOW in this refactor:**

1. **`= default` for comparison operators** — add to value structs that stay as structs
2. **`final` on leaf classes** — prevent unintended inheritance (C.139)
3. **`override` on all virtual overrides** — already standard practice; ensure new interface implementations use it
4. **`std::unique_ptr` for owned interface implementations** — modern ownership semantics

**What to explicitly NOT use yet:**

1. **Modules** — clang-cl C++20 module support is not production-ready for MSVC-style targets. The `#pragma once` approach works and doesn't block encapsulation goals (private data in headers is already hidden).
2. **C++26 Contracts** — compiler support is nascent. Add `// Precondition:` / `// Postcondition:` comments in interface headers as documentation for future contract annotations.
3. **`std::expected`** — not worth the refactoring churn to replace `eh::Result` across the entire codebase. The existing error handling pattern is consistent and well-tested.

### 6. Patterns to Follow (Composition Over Inheritance)

**Recommended structural patterns for the pack subsystem:**

```
PackService (orchestrator — owns dependencies)
    │
    ├── PackPlan (value object — stays as struct/aggregate with accessors)
    │
    ├── IZipWriter (abstract interface)
    │   └── LibzipppWriter (production impl, wraps libzippp)
    │
    ├── Packer (stateless grouping algorithms → stays free functions or static methods)
    │
    └── JobState::Store (already a class — inject via shared_ptr)
```

**Class relationships — favor composition:**

```cpp
// GOOD: Composition (has-a)
class PackService {
    std::unique_ptr<IZipWriter> zipWriter_;  // owns via unique_ptr
    appctx::AppContext& ctx_;                 // references (non-owning)
    // ...
};

// AVOID: Deep inheritance for domain objects
// class PackService : public TaskExecutor, public ErrorHandler, ...  ← BAD
```

**Single responsibility per class:**
- `PackService` → orchestrates pack workflow (run, packGroups)
- `Packer` (or free functions in `packer.cpp`) → file grouping algorithms
- `LibzipppWriter` → wraps libzippp, implements IZipWriter
- `PackPlan` → value object, carries plan configuration

## Anti-Patterns — What NOT to Introduce

### Critical Anti-Patterns (rewrite-causing)

| Anti-Pattern | Why It's Dangerous | What to Do Instead |
|-------------|-------------------|-------------------|
| **Deep Inheritance Hierarchy (>2 levels)** | C.138: "Use `final` on leaf classes." Deep hierarchies create fragile base class problems, make testing harder, and the codebase has zero inheritance currently. | Max 2 levels: Interface → Implementation. Use composition for everything else. |
| **Virtual Functions in Hot Paths** | The `packFilesToZip` loop processes potentially millions of files. Virtual dispatch in inner loops defeats branch prediction and inlining. | Make interfaces at the workflow level, not the per-file level. The `IZipWriter` interface should have coarse-grained methods (`addFile`, `finalize`), not per-byte virtual calls. |
| **God Class (everything in one class)** | PackService that knows about FFmpeg, zip, filesystem, progress bars, AND error handling becomes untestable and unmaintainable. | Single Responsibility: Split into focused classes. PackService orchestrates; it doesn't encode video. |
| **Private Data + Public Getters/Setters for EVERY Field** | Violates "Tell, Don't Ask." If a class exposes all its internal state via getters/setters, it hasn't actually encapsulated anything. | Expose behavior, not state. `plan.zipName()` not `plan.getOutputDir()`. |
| **Breaking the `static_assert(is_aggregate_v<PackPlan>)` Without Migration** | At least 4 test sites + production code use designated initializers. Removing the assertion without migrating all callers breaks the build. | Phase 1: Migrate all designated-initializer callers to factory/constructor. Phase 2: Remove `static_assert`. Phase 3: Add `private` members. |
| **Over-Mocking (mock everything)** | The codebase's existing tests with `TempDir` + real filesystem are stable (909 assertions pass). Introducing mocks for things that don't need mocking adds maintenance burden and false confidence. | Only mock external side effects (zip I/O, process execution). Never mock value types or standard library facilities. |

### Moderate Anti-Patterns

| Anti-Pattern | Prevention |
|-------------|-----------|
| **Virtual Inheritance (diamond)** | Not needed. Use single interface inheritance only. |
| **`friend` for Testing** | Use interfaces or make testable methods `public`. `friend class TestFixture` is a smell — it means your class has hidden dependencies. |
| **Static Mutable State** | Already absent from the codebase. Don't introduce. Use constructor injection instead of singletons. |
| **Base Class with Data Members** | C.121: interface classes should have zero data members. Production base classes with data → use composition. |
| **Exception-Throwing Destructors** | C.36: destructors must not fail. All the IZipWriter, IProcessRunner destructors must be `noexcept`. |
| **Copyable Interfaces** | Interfaces should delete copy constructor/assignment. Use `unique_ptr<Interface>` for ownership transfer. |

### Minor Anti-Patterns

| Anti-Pattern | Prevention |
|-------------|-----------|
| **`protected` Data Members** | C.133: avoid `protected` data. Use `private` + `protected` accessor methods if truly needed by derived classes. |
| **Non-Virtual Destructor in Base** | C.35, C.127: every interface class must have `virtual ~Interface() = default;` |
| **Type Erasure via `std::function` for Hot Paths** | `std::function` has allocation overhead. For callbacks in inner loops, use templates or `std::move_only_function` (C++23). |
| **`dynamic_cast` in Application Code** | C.146: use `dynamic_cast` only where you can't use virtual functions. If you find yourself `dynamic_cast`ing, the interface is wrong. |

## Stack Patterns by Scenario

**If the struct has NO invariants (pure data bundle):**
- Keep as `struct` with all public members
- Examples: `PackGroupInput`, `PackEntryInput`, `PackGroupPartition`, `PackEntryPartition`
- Add `auto operator==(T const&) const -> bool = default;` for testability
- These are "Category A" per C.134 — no encapsulation needed

**If the struct has invariants but is small (1-3 interconnected fields):**
- Convert to `class` with private data + constructor that enforces invariant
- Provide const accessor methods for the data consumers need
- Example: `PackPlan` — `groups` and `outputDir` are tightly coupled (groups must correspond to output paths)

**If the free function accesses no private state:**
- Keep as a free function (possibly in a namespace, not anonymous)
- Move to utility/algorithm header if reusable across modules
- Example: `appendOrdinalRangeSuffix` — pure string manipulation, should stay free

**If the free function accesses private state or is tightly coupled to a class's behavior:**
- Make it a `private` method (or `public` if it's part of the class's interface contract)
- If it's currently in an anonymous namespace → `private static` member
- Example: `countPackedFiles` — called only by pack service logic

**If the dependency is an external system (zip, FFmpeg):**
- Define a pure abstract interface (C.121)
- Provide production implementation + test mock
- Inject via constructor (unique_ptr for ownership, shared_ptr for shared state)
- Example: `IZipWriter` for libzippp

**If the dependency is a simple algorithm or computation:**
- Pass as a `std::function` or template callable — no interface needed
- The codebase already does this with `ZipEntryNameResolver`, `PackEntryProgressCallback`
- Example: `zipNameForIndex`, `progressLabelForIndex` in PackPlan

## Existing Patterns to Leverage (Not Rebuild)

The codebase already has good patterns that should be extended, not replaced:

| Existing Pattern | Where | How to Extend |
|-----------------|-------|---------------|
| `jobstate::Store` — class with private data, constructor DI, public methods | `src/core/job_state.h` | Model for new classes. Follow its constructor injection style. |
| `RuntimeContext::VideoInfoCacheStore` — struct-as-value with methods | `src/core/app_context.h` | Pattern for small encapsulated value types with behavior. |
| `PackPlan` with `std::function` callbacks | `src/pack/pack_service.h` | Already using strategy pattern via callbacks. Extend by wrapping in a class. |
| `TempDir` test helper | `tests/test_utils.h` | Already provides test isolation. Continue using; complement with mock interfaces for non-filesystem deps. |
| `eh::Result<T>` error handling | `src/core/error_handle.h` | Consistent, tested. Keep using for this milestone. |
| Catch2 `TEST_CASE` with designated initializers | `tests/pack_service_tests.cpp` | Keep tests passing DURING refactor (RED→GREEN cycles). |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| C++ Modules (C++20) | clang-cl module support is not production-stable for MSVC-ABI targets | `#pragma once` + header guards (already working) |
| `std::expected` (C++23) | Would require touching all call sites of `eh::Result` | `eh::Result<T>` (already consistent and tested) |
| C++26 Contracts | Compiler support is nascent (no production clang-cl implementation yet) | `// Precondition:` / `// Postcondition:` comments in headers |
| CRTP (Curiously Recurring Template) | Adds template complexity with no benefit for this codebase | Simple virtual dispatch (not in hot paths) |
| Singleton/Monostate patterns | Hides dependencies, makes testing hard | Constructor injection with `AppContext&` |
| `std::function` for inner-loop callbacks | Allocation overhead in hot paths | Template callables or `std::move_only_function` (C++23) |
| Boost.DI or other DI frameworks | Adds external dependency complexity for no benefit | Manual constructor injection (already used in `jobstate::Store`) |
| PIMPL for small domain objects | Indirection overhead; the codebase isn't a library with ABI stability needs | Direct `private` members (recompilation cost is acceptable for an application) |

## Sources

| Source | Type | Confidence | Topics Verified |
|--------|------|-----------|-----------------|
| `/isocpp/cppcoreguidelines` (Context7) | Official | HIGH | C.134 (struct vs class), C.121 (pure abstract interfaces), I.25 (interface segregation), C.35/C.127 (virtual destructors), C.133 (protected data), C.138 (final), C.146 (dynamic_cast), C.4 (method vs free function), PIMPL idiom, NVI pattern |
| `/refactoringguru/design-patterns-cpp` (Context7) | Educational | MEDIUM | Strategy, Adapter, Facade patterns with C++ examples |
| cppreference.com — Modules | Official | HIGH | C++20 modules syntax, module linkage, export/import semantics, compiler support status |
| isocpp.github.io/CppCoreGuidelines | Official | HIGH | Full guidelines including Appendix B (Modernizing code), enforcement profiles, gradual adoption philosophy |
| Existing codebase analysis | Primary | HIGH | Current patterns: `jobstate::Store` (class with DI), `PackPlan` (aggregate with static_assert), anonymous namespace free functions, Catch2 test patterns, `eh::Result` error handling |

---

*Stack research for: C++ OO encapsulation refactoring of encro pack subsystem*
*Researched: 2026-04-29*
*Confidence: HIGH — backed by C++ Core Guidelines (official), codebase direct analysis, and cppreference.com*
