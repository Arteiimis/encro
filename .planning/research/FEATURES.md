# Feature Research

**Domain:** C++ Pack/Archiving Subsystem OO Refactoring
**Researched:** 2026-04-29
**Confidence:** HIGH

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = refactoring feels half-done and untrustworthy.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Core structs → class encapsulation** | The milestone explicitly calls for `struct` → `class` conversion with private data members. Without this, the refactoring is cosmetic. | MEDIUM | `PackPlan`, `CompactProgressState`, and the service-layer types must hide data behind const-qualified accessors. Pure-data structs (`PackFileEntry`, `FileOrdinalRange`, `PackRunResult`) remain structs per C++ Core Guidelines C.2. |
| **Free functions consolidated into class methods** | `pack_service.cpp` has `packGroups()`, `packGroupsCompact()`, `packGroupsFull()`, `runPackPlan()` as free functions in `pack::` namespace. `packer.cpp` has `packFilesToZip()` (3 overloads), `groupFilesBySize()`, etc. They logically belong to cohesive classes. | MEDIUM | Consolidation must preserve the existing 909 assertions. Free functions in anonymous namespaces (helper functions) stay as private methods or file-local helpers. |
| **Clear public API on headers** | Headers currently expose implementation details (internal structs, all helpers). A well-done OO refactoring exposes only the public interface in headers; implementation details move to `.cpp`. | LOW | Forward-declare internal types. Use PIMPL or opaque pointers only where truly needed (not everywhere — that's over-engineering). |
| **PackPlan construction invariant enforcement** | Currently `PackPlan` is a designated-initializer aggregate with `static_assert(std::is_aggregate_v<...>)`. After `class` encapsulation, construction must validate: groups non-empty, outputDir valid, callback functions non-null where required. | MEDIUM | `static_assert` must be removed or restructured. Replace designated initializers with either: (a) constructor with validation, or (b) Builder pattern if construction is complex. |
| **All 909 assertions across 215 test cases pass** | The entire point of this milestone is zero behavioral regression. Tests are the safety net. | HIGH (effort, not complexity) | Tests must be updated to use new API, but assertions and coverage must not decrease. This is a constraint, not a feature to build. |
| **Global-scope structs moved into `pack::` namespace** | `PackGroupInput`, `PackGroupPartition`, `PackEntryInput`, `PackEntryPartition` are defined in the global namespace in `packer.h`. This leaks from the pack subsystem. | LOW | Move into `pack::` with appropriate access control. These are input/output types — may stay as structs but must live in `pack::`. |
| **`appctx::AppContext` dependency made explicit and injectable** | `pack_service.cpp` takes `appctx::AppContext&` by reference in `runPackPlan()`. The dependency should be explicit in the constructor or method signatures, and testable by providing minimal context. | MEDIUM | Don't hide AppContext behind an interface — extract only what pack subsystem needs (job state store, config flags) as constructor parameters. |

### Differentiators (Competitive Advantage)

Features that set the refactoring apart. Not required, but make the result significantly better than "adequate."

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Progress reporting via injected strategy** | Currently `PackPlan` carries 6 `std::function` callbacks for progress (`onGroupStart`, `onGroupSuccess`, `onGroupFailure`, `onCompactProgress`, `onCompactStatusText`). A `PackProgressSink` abstract concept (or `std::function` bundle struct) decouples the plan from UI concerns and enables test mocking. | MEDIUM | This is "differentiator" because we could keep `std::function` members and still achieve encapsulation. But separating progress into an injectable sink makes testing far easier and the architecture cleaner. Use a struct of callbacks, not virtual interface — idiomatic C++. |
| **Builder pattern for PackPlan construction** | Designated initializers are gone after `class` encapsulation. A `PackPlan::Builder` (or `makePackPlan()` factory) provides: named construction steps, validation at `.build()`, and immutability of the resulting plan. | MEDIUM | The current `buildDirectoryPackPlan()` in `packer.cpp` is effectively a 100-line builder. Extracting this into a proper builder class with clear stages improves readability and testability. |
| **Dependency injection for zip operations — mock boundary** | `packer.cpp` directly uses `libzippp::ZipArchive` inline. Extracting zip I/O behind a `ZipArchiveHandle` (concrete class wrapping libzippp, NOT a virtual interface) enables: (a) testing without real zip files, (b) future swap of zip library. | HIGH (design) | Critical boundary. Tests currently create real zip files. A thin `ZipWriter` class wrapping `libzippp::ZipArchive` with RAII semantics allows injecting a `NullZipWriter` or `InMemoryZipWriter` in tests. This is the single biggest testability win. |
| **RAII resource management for zip archive lifecycle** | `packFilesToZip()` currently manually calls `zip.open()` / `zip.close()` with try/catch. An RAII wrapper guarantees close on scope exit, eliminating the need for manual cleanup. | MEDIUM | Complements the `ZipWriter` class. The `CompactProgressState` already uses RAII-like patterns (initBar/startSpinner/finish) — formalize this. |
| **`PackService` takes configuration, returns results — no hidden state** | A service class that is constructed with its configuration (output dir, max parallel jobs, compact flag), receives a plan, and produces a result. No mutable state between invocations. Each call is self-contained. | MEDIUM | The current `packGroupsCompact` has internal `CompactProgressState` that's created per-call — this is correct. Formalize the stateless-service pattern. |
| **Compile-time polymorphism via templates where virtual would be overkill** | For the progress sink and zip handle, prefer `template<typename Sink>` over virtual base class. Zero runtime overhead, better inlining. | MEDIUM | Use `std::invocable` concepts (C++20) or simple template parameters. Only use virtual if runtime polymorphism is truly needed (it isn't, for this subsystem). |
| **Header-only pure-data types, implementation-only service types** | `PackFileEntry`, `FileOrdinalRange`, `PackRunResult` remain header-only structs. `PackService`, `Packer`, `ZipWriter` are declared in headers but fully implemented in `.cpp`. Clean separation of data from behavior. | LOW | Aligns with C++ Core Guidelines C.2: use `struct` when there's no invariant, `class` when behavior needs to maintain invariants. |

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem good but create problems.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| **I-prefix virtual interfaces (`IPackService`, `IZipWriter`, `IProgressSink`)** | Familiar from Java/C#. "Makes everything mockable." | Virtual dispatch overhead for no benefit. C++ has templates and `std::function` for polymorphism. Interface proliferation creates header dependency explosion. Violates C++ Core Guidelines: "These guidelines are emphatically not meant to define a Java-like subset of C++." | Use concrete classes with `std::function` callbacks for hot-swap behavior. Use templates for compile-time polymorphism. If mocking is needed, mock at the `ZipWriter` boundary with a thin concrete wrapper, not an interface hierarchy. |
| **Deep inheritance hierarchies** | "Reuse" via `BasePackService` → `PackService` → `CompactPackService`. | Tight coupling, fragile base class problem, hard to reason about virtual dispatch. C++ Core Guidelines C.120: "Use class hierarchies only if you have a genuine need for runtime polymorphism." We don't. | Composition over inheritance. `PackService` has a `ProgressSink` (composition), a `ZipWriter` (composition), not an "is-a" chain. |
| **Dependency injection container / framework** | "Decouples everything." | Massive over-engineering for a subsystem with ~5 dependencies. Adds build complexity, header bloat, slow compile times. Obscures the actual dependency graph. | Manual constructor injection. `PackService` constructor takes what it needs (output dir, max parallelism, progress sink). Transitive dependencies are composed at the call site (main/app pipeline). |
| **PIMPL for every class** | "ABI stability, fast compilation." | Doubles allocation for every object. Indirection for every field access. PIMPL is a tool for library ABI boundaries, not internal subsystem organization. The `pack::` subsystem is internal — no ABI stability contract. | Use PIMPL only if measured compilation time is a real problem. Forward-declare where possible. Use `#pragma once` (already done). |
| **`operator<<` overloading for logging** | "Nice syntax." | Binds logging format to type definition. Creates include dependency on `<ostream>`. Log format changes should not require recompilation of type definition. | Keep `spdlog::debug()` calls as they are (already working well). If structured logging is needed, add a `to_string()` or `format_to()` method on the type. |
| **Templates everywhere for "generic reusability"** | "Could pack anything, not just zip." | Premature generalization. The subsystem packs zip files. If (big if) tar/7z support is added, refactor then with real requirements. Template metaprogramming for hypothetical future use adds complexity with zero current value. | Keep concrete — `packFilesToZip` takes zip-specific parameters. If future formats arrive, extract commonality at that point with real requirements. |
| **Every struct becomes a class with getters/setters** | "Consistency." | `PackFileEntry` (2 fields), `FileOrdinalRange` (3 fields), `PackRunResult` (2 fields) are pure data bags. Adding getters adds boilerplate with zero invariant enforcement. C++ Core Guidelines C.2: "Use class if the class has an invariant; use struct if the data members can vary independently." | Keep pure-data types as structs with public members. Only encapsulate types that have invariants (`PackPlan`, `CompactProgressState`, `ZipWriter`). |

## Feature Dependencies

```
[Struct→class encapsulation for PackPlan]
    └──requires──> [PackPlanBuilder or validated constructor]
                        └──requires──> [Removal/restructure of static_assert(is_aggregate_v<PackPlan>)]

[ProgressSink (injected callback bundle)]
    └──enables──> [Mock testing of progress without real ProgressContext]
    └──enables──> [Cleaner PackPlan (no 6 std::function members)]

[ZipWriter (RAII wrapper around libzippp)]
    └──enables──> [Mock testing of zip creation without real files]
    └──enables──> [RAII resource management]
    └──enables──> [Cleaner Packer class (no raw libzippp calls)]

[PackService class]
    └──requires──> [PackPlan construction settled (builder or constructor)]
    └──requires──> [ProgressSink or callback struct]
    └──requires──> [ZipWriter for actual zip creation]

[Packer class (grouping logic + zip writing)]
    └──requires──> [ZipWriter]
    └──requires──> [Global structs moved to pack:: namespace]

[All test cases pass]
    └──requires──> [Every feature above completed]
    └──constrains──> [API surface must be compatible or have migration path]
```

### Dependency Notes

- **Struct→class encapsulation requires PackPlan builder/constructor:** The existing `static_assert(std::is_aggregate_v<pack::PackPlan>)` enforces aggregate usage. Once we make data private, designated initializers fail. We must provide an alternative construction mechanism FIRST, then encapsulate.
- **ZipWriter enables mock testing:** This is the highest-leverage differentiator. Without it, tests continue to create real zip files on disk (slow, fragile). With it, tests can verify "was addFile called with correct parameters?" in milliseconds.
- **PackService requires ProgressSink settled:** Currently `packGroupsCompact()` reads `plan.onCompactProgress`, `plan.onCompactStatusText` etc. If we move these off PackPlan, PackService must receive them through a different channel. Both approaches work — settle this design decision early.
- **Global structs moved to `pack::` is a low-effort prerequisite:** These are in the global namespace in `packer.h`. Moving them into `pack::` is a mechanical change that should happen first to avoid naming conflicts as we add more `pack::` types.

## MVP Definition

### Launch With (v1.3 Core — Must Complete)

Minimum refactoring that delivers the milestone's value. If we only do this, it's a success.

- [ ] **PackPlan class encapsulation with validated construction** — Private data, public const accessors, constructor or `.build()` that enforces invariants (groups non-empty, outputDir set, callbacks valid). Removal of aggregate `static_assert`.
- [ ] **Free functions consolidated into classes** — `packGroups()`, `packGroupsCompact()`, `packGroupsFull()` → `PackService` methods. `packFilesToZip()` (3 overloads) → `Packer` or `ZipPacker` methods. `groupFilesBySize()`, `groupPackFiles()`, etc. → `PackGrouper` methods or `Packer` grouping methods.
- [ ] **Progress reporting decoupled from PackPlan** — Extract the 6 `std::function` callbacks into a `PackProgressCallbacks` struct (or inject into service). PackPlan carries data; progress is a separate concern.
- [ ] **ZipWriter RAII wrapper** — A thin concrete class that wraps `libzippp::ZipArchive` with RAII open/close. NOT a virtual interface. This is the mock boundary for testing.
- [ ] **Global structs into `pack::` namespace** — `PackGroupInput`, `PackGroupPartition`, `PackEntryInput`, `PackEntryPartition` moved from global to `pack::`.
- [ ] **All 909 assertions pass** — Non-negotiable. Tests updated to use new API; no assertion removed without replacement.

### Add After Validation (v1.3 Polish — Nice to Have)

Features to add once core refactoring is stable and tests pass.

- [ ] **PackPlan builder pattern** — If constructor parameter list grows unwieldy, extract a `PackPlan::Builder` with fluent API. Only do this if the constructor approach proves awkward.
- [ ] **CompactProgressState → class with private members** — Already a proto-class in the anonymous namespace. Formalize with private fields and clear lifecycle (init → update → finish).
- [ ] **PackService stateless pattern formalized** — Document and enforce that `PackService` has no mutable state between calls.

### Future Consideration (v2+)

Features to defer until the refactoring proves itself in production.

- [ ] **InMemoryZipWriter for tests** — A test-only zip writer that captures entries in memory instead of writing to disk. Enables ultra-fast tests. Requires ZipWriter to be a non-virtual concrete type (use `std::variant` or template parameter).
- [ ] **Template-based pack format abstraction** — If (and only if) we add tar/7z support. Refactor with real requirements, not hypothetical ones.
- [ ] **PIMPL for public API stability** — Only if we decide to ship `pack::` as a library with ABI guarantees. Not needed for internal CLI tool.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| PackPlan class encapsulation | HIGH — core milestone deliverable | MEDIUM | P1 |
| Free functions → class methods | HIGH — core milestone deliverable | MEDIUM | P1 |
| Progress decoupled from PackPlan | HIGH — enables testing, cleaner design | MEDIUM | P1 |
| ZipWriter RAII wrapper | HIGH — single biggest testability win | HIGH | P1 |
| Global structs → pack:: | MEDIUM — code organization | LOW | P1 |
| Builder pattern for PackPlan | MEDIUM — nice but constructor works | MEDIUM | P2 |
| CompactProgressState encapsulation | MEDIUM — internal quality | LOW | P2 |
| InMemoryZipWriter for tests | MEDIUM — faster test suite | MEDIUM | P3 |
| Template-based format abstraction | LOW — no current need | HIGH | P3 |
| PIMPL for ABI stability | LOW — no ABI contract | HIGH | P3 |

**Priority key:**
- P1: Must complete for v1.3 milestone success
- P2: Should complete if time permits within v1.3
- P3: Defer to future milestone

## Competitor Feature Analysis

This section is adapted for a refactoring context — "competitors" are alternative refactoring approaches observed in C++ codebases.

| Feature | Java-style OO (over-engineered) | C-with-namespaces (under-engineered) | Our Approach (idiomatic C++) |
|---------|------|------|------|
| Progress reporting | `IProgressObserver` virtual interface with `update()` method | Raw function pointers or global callback registry | `PackProgressCallbacks` struct of `std::function` — zero-overhead, composable, mockable |
| Zip I/O | `IZipArchive` interface → `LibzipppArchive` → `MockArchive` | Direct libzippp calls everywhere, duplicated error handling | `ZipWriter` concrete RAII class — single responsibility, swappable via template or link-time substitution |
| Plan construction | `PackPlanFactory` → `AbstractPackPlanFactory` → `DefaultPackPlanFactory` | Memset to zero, fill fields manually, hope invariants hold | Constructor with validation OR `PackPlan::Builder` — validate once at construction, immutable after |
| Service layer | `PackService` inherits `IService` with `initialize()`/`execute()`/`shutdown()` lifecycle | Free function `packGroups(plan)` | `PackService` stateless class — construct with config, call `execute(plan)`, no lifecycle |
| Grouping logic | `GroupingStrategy` → `SizeBasedGroupingStrategy` → `KeepSourceDirGroupingStrategy` | Functions with 7 parameters and bool flags | Functions grouped into a `PackGrouper` class OR kept as free functions in `pack::detail` namespace — no need for Strategy pattern |
| Test doubles | Mock framework (GoogleMock) with `EXPECT_CALL` on virtual methods | Real zip files on disk for every test | Concrete `ZipWriter` with `NullZipWriter` (no-op) or `RecordingZipWriter` (captures calls) — no virtual, no framework |

## Sources

- **C++ Core Guidelines (isocpp.github.io/CppCoreGuidelines):** Rules C.1-C.9 on class design, C.2 (struct vs class), C.120 (hierarchy only for runtime polymorphism), P.11 (encapsulate messy constructs). Accessed 2026-04-29.
- **Existing encro codebase:** `src/pack/pack_service.h`, `src/pack/pack_service.cpp`, `src/pack/packer.h`, `src/pack/packer.cpp`, `src/core/app_context.h`, `src/core/progress.h`, `src/core/archive_plan.h`, `tests/pack_service_tests.cpp`, `tests/packer_tests.cpp`. All read 2026-04-29.
- **PROJECT.md v1.3 milestone definition:** Explicit goals for struct→class, free function consolidation, mock boundaries, 909 assertion preservation.
- **Prior milestone decisions (v1.1, v1.2):** Individual typed parameters (no context structs), factory function pattern, 2-level lambda nesting acceptable, TDD RED gate cycles. These inform what patterns the codebase already accepts.

---

*Feature research for: Pack Subsystem OO Refactoring (v1.3)*
*Researched: 2026-04-29*
