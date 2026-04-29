# Project Research Summary

**Project:** encro — C++26 CLI Media Encoder/Archiver
**Domain:** C++ procedural-to-OO refactoring of the pack subsystem
**Researched:** 2026-04-29
**Milestone:** v1.3 — Pack Subsystem OO Refactoring
**Confidence:** HIGH

## Executive Summary

This research evaluates how to incrementally refactor the encro pack subsystem from a C-style architecture (public structs + free functions + anonymous-namespace helpers + raw libzippp calls) to idiomatic C++ encapsulated classes with injectable dependencies and mock boundaries — without breaking the existing 909 assertions across 215 test cases.

**The recommended approach is behavior-first encapsulation, not data-first.** The OO value comes from wrapping the 30+ free functions and anonymous-namespace helpers in `pack_service.cpp` and `packer.cpp` into cohesive service classes (`PackService`, `Packer`) with clear responsibilities, not from making every struct private-and-gettered. Pure data-transfer types (`PackPlan`, `PackFileEntry`, `PackRunResult`, etc.) remain aggregates — this is consistent with C++ Core Guidelines C.2 and C.134. Service classes consume data via `const&`, own dependencies via constructor injection, and expose coarse-grained behavioral methods.

**A critical researcher conflict was resolved during synthesis:** Three research files proposed making `PackPlan` a class with private data and a Builder pattern. PITFALLS.md forcefully demonstrated this would be a 100% build break with zero incremental recovery path — `static_assert(std::is_aggregate_v<pack::PackPlan>)` at pack_service.h:52 and 16 designated-initializer construction sites (6 production, 10 test) make PackPlan non-negotiable as an aggregate. The resolution: **PackPlan stays an aggregate struct; encapsulation focuses on service classes that consume PackPlan by `const&`.** This preserves both the existing contract and the refactoring's real value.

The primary risk is over-engineering: adding virtual dispatch on hot packing paths, proliferating interfaces where templates or concrete classes suffice, or converting pure-data types to getter/setter ceremony. All four research files independently warn against this. Mitigation: mark all pack classes `final`, keep all method bodies in `.cpp` files, and use exactly ONE interface (`IPacker` for zip I/O mocking) rather than a deep interface hierarchy. The second risk is wrong migration order — starting with PackPlan before its consumers. Mitigation: follow the "leaves → trunk" ordering advocated by PITFALLS.md: extract shared types first, then TU-local helpers, then service classes, then migrate consumers.

## Key Findings

### Recommended Stack (from STACK.md)

The refactoring should use incremental, composition-based patterns rather than deep inheritance or framework-based DI. The codebase already contains a strong precedent in `jobstate::Store` (class with private data, constructor DI, public methods). This pattern should be extended, not replaced.

**Core patterns:**
- **Struct preservation for data bundles:** Types with no invariants (`PackFileEntry`, `PackRunResult`, `FileOrdinalRange`) stay as public-data `struct`s per C.134. PackPlan stays aggregate (see conflict resolution below). Only types with behavioral invariants become `class`es.
- **Free function → method migration via facade:** Keep old free-function signatures as `[[deprecated]]` wrappers delegating to new class methods during transition. Consumers migrate one subsystem at a time.
- **Constructor injection (no DI framework):** Dependencies are passed via constructor parameters. `PackService(appctx::AppContext& ctx, std::unique_ptr<IPacker> packer)`. No Boost.DI, no service locator, no singletons.
- **One abstract interface (`IPacker`):** For zip I/O mockability only. The codebase's Catch2 tests already use `TempDir` + real filesystem effectively — only mock external side effects. Progress reporting stays as `std::function` callback structs (zero-overhead, composable), NOT virtual interfaces.
- **No C++20 modules, contracts, or `std::expected` yet:** Compiler support is nascent. Use `#pragma once`, `// Precondition:` comments, and the existing `eh::Result<T>`.
- **All pack classes marked `final`:** Prevents accidental inheritance hierarchies and enables devirtualization with LTO.

### Expected Features (from FEATURES.md)

**Must have (P1 — v1.3 Core):**
- Free functions consolidated into `PackService` and `Packer` class methods
- PackPlan remains aggregate; construction validation lives in factory functions
- Progress callbacks extracted from PackPlan into injectable `PackProgressCallbacks` struct
- `ZipWriter` RAII wrapper around `libzippp::ZipArchive` — the mock boundary for testing
- Global-scope structs (`PackGroupInput`, `PackGroupPartition`, `PackEntryInput`, `PackEntryPartition`) moved into `pack::detail::` namespace
- All 909 assertions pass — zero behavioral regression

**Should have (P2 — v1.3 Polish):**
- PackPlan builder/factory validation formalized (already partially done by `buildDirectoryPackPlan`)
- `CompactProgressState` formalized as a private class within PackService
- Stateless-service pattern documented and enforced

**Defer (v2+):**
- InMemoryZipWriter for ultra-fast tests
- Template-based pack format abstraction (tar/7z — only if needed)
- PIMPL for ABI stability (not needed for internal CLI tool)

### Architecture Approach (from ARCHITECTURE.md)

The refactoring structures the pack subsystem into three focused classes with one supporting interface:

1. **`pack::PackPlan`** (aggregate struct — UNCHANGED) — Data-transfer object carrying zipping configuration (groups, output directory, callbacks, flags). Passed as `const&` to all service methods. The `static_assert(is_aggregate_v)` remains.

2. **`pack::PackService`** (class) — Orchestration. Takes `AppContext&` and `unique_ptr<IPacker>` via constructor injection. Methods: `packGroups(plan)`, `runPackPlan(ctx, plan)`, `selectPackPlanIndexes(plan, indexes)`. Owns progress callback wiring and resumability logic. Anonymous-namespace `packGroupsCompact`/`packGroupsFull`/`resolveZipNameForIndex` become private methods.

3. **`pack::IPacker`** (abstract interface) — Zip I/O contract. Method: `packFilesToZip(entries, zipPath, callbacks)`. Enables unit testing PackService without real zip archives.

4. **`pack::Packer`** (class, implements IPacker) — Concrete zip I/O using libzippp. Also owns file grouping algorithms (`groupFilesBySize`, `groupPackFiles`, `buildDirectoryPackPlan`). These grouping functions are genuinely reusable across video/picture — they remain as public methods or free functions in `pack::`.

**Component boundaries:**
- Public API: `pack_types.h` (value types), `pack_plan.h` (PackPlan struct), `pack_service.h` (PackService), `packer_interface.h` (IPacker), `packer.h` (Packer)
- Internal: `packer_types.h` (PackGroupInput etc. in `pack::detail::`)
- Temporary: `pack_facade.h` (deprecated free-function wrappers for backward compatibility)
- All method definitions in `.cpp` files — zero inline/virtual in headers

**Circular dependency resolved:** Extract `pack_types.h` first (PackFileEntry, FileOrdinalRange, PackRunResult). Both `pack_service.h` and `packer.h` include only `pack_types.h` and `packer_interface.h`, never each other.

### Critical Pitfalls (from PITFALLS.md)

1. **Breaking the PackPlan aggregate contract** — Making PackPlan non-aggregate causes 16 simultaneous build failures with zero incremental recovery. **Avoid by:** keeping PackPlan as an aggregate struct. Encapsulate the logic in service classes that take `PackPlan const&`.

2. **Virtual dispatch on the hot pack path** — `packFilesToZip` is called per-file in potentially thousands-of-file batches. Virtual calls prevent inlining and LTO optimization. **Avoid by:** zero virtual functions in v1.3. Mark all classes `final`. Use template callables or `std::function` for behavioral variation (already done correctly with the `compact` bool flag).

3. **Test breakage from moving functions to `private:`** — Anonymous-namespace functions currently accessible within the TU become inaccessible to tests when moved to private class methods. **Avoid by:** keeping the existing public API surface intact. Add integration tests that exercise the full path before refactoring as a canary.

4. **Getter/setter proliferation for every field** — C++ Core Guidelines call this an anti-pattern. For PackPlan's 11 fields, 22 accessor functions add ceremony with zero encapsulation. **Avoid by:** keeping data-transfer types as structs. Only encapsulate types that have behavioral invariants and methods.

5. **Wrong migration order (PackPlan before consumers)** — Changing PackPlan first forces simultaneous changes across 6 consumer files + 10 test sites. **Avoid by:** "leaves → trunk" ordering: TU-local helpers first, then service classes, PackPlan stays unchanged.

## Implications for Roadmap

Based on combined research, the suggested four-phase structure follows the "leaves → trunk" approach. Each phase must compile independently and pass all 909 assertions before proceeding.

### Phase 1: Type Extraction & Namespace Cleanup (Foundation)
**Rationale:** This is the lowest-risk phase — pure mechanical refactoring with zero behavioral changes. It breaks the `packer.h` → `pack_service.h` circular dependency by extracting shared types into a common header, and moves global-scope pollution into the `pack::` namespace. Completing this first creates a clean foundation for all subsequent encapsulation work.

**Delivers:**
- `src/pack/pack_types.h` — PackFileEntry, FileOrdinalRange, PackRunResult extracted from pack_service.h
- Global-scope structs (PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition) moved to `pack::detail::` namespace in `src/pack/packer_types.h`
- `packer.h` no longer includes `pack_service.h` — circular dependency resolved
- All existing code compiles unchanged; all 909 assertions pass
- Zero consumer changes; zero production code changes beyond `#include` adjustments

**Avoids:** Pitfall #3 (circular includes), Pitfall #6 (wrong migration order — starts at leaves, not trunk)

**Research needed:** Minimal — this is a mechanical refactoring with well-documented patterns. The exact list of shared types vs. internal types needs codebase audit, but no design research is required.

### Phase 2: Service Class Extraction (Core Encapsulation)
**Rationale:** This is where the OO value materializes. Free functions and anonymous-namespace helpers in `pack_service.cpp` and `packer.cpp` are consolidated into `PackService` and `Packer` classes. PackPlan stays an aggregate struct — it's consumed by `const&`. A facade layer (`pack_facade.h`) provides backward-compatible `[[deprecated]]` wrappers so consumers don't change yet. This phase can be split into multiple sub-PRs: (2a) Packer class, (2b) PackService class, (2c) facade + integration.

**Delivers:**
- `Packer` class (implements zip I/O via libzippp, owns grouping algorithms)
- `PackService` class (orchestration: `packGroups`, `runPackPlan`, `selectPackPlanIndexes`)
- `CompactProgressState` becomes private nested helper inside PackService
- Anonymous-namespace helpers become private methods
- `pack_facade.h` — `[[deprecated]]` free-function wrappers delegating to new classes
- Progress callbacks extracted from PackPlan call sites into PackService constructor
- All 909 assertions pass through old free-function API (facade) AND new class API (tested directly)

**Uses:** Constructor injection pattern from STACK.md (no DI framework), `final` on all pack classes, method bodies in `.cpp` only

**Avoids:** Pitfall #1 (PackPlan stays aggregate), Pitfall #2 (zero virtual), Pitfall #5 (no getter/setter proliferation), Pitfall #6 (consumers unchanged via facade)

**Research needed:** Phase 2a/2b design review — which anonymous-namespace functions become `private` methods vs. `private static` methods vs. remain as free functions in `pack::detail::`. Use the classification table from STACK.md (lines 117-124) as the starting point. Phase 2c (facade validation) needs a test plan to ensure old and new code paths produce identical results.

### Phase 3: Dependency Injection & Testability
**Rationale:** With service classes established and verified, this phase opens the mock boundary. `IPacker` interface is introduced, `Packer` implements it, `PackService` depends on the interface. A `MockPacker` enables unit testing PackService without real zip I/O. This is the single biggest testability win — tests that currently create real zip files on disk become sub-millisecond verifications of captured method calls.

**Delivers:**
- `IPacker` abstract interface (single interface — not a hierarchy)
- `Packer` implements `IPacker` (production)
- `MockPacker` implements `IPacker` (test)
- `PackService` constructor takes `unique_ptr<IPacker>` (constructor injection)
- `ZipWriter` RAII wrapper (production implementation delegates to libzippp; could be internal to Packer)
- New unit tests: PackService behavior verified with injected MockPacker
- Packer tests remain integration-style (real zip files via TempDir) — complement, don't replace

**Uses:** C++ Core Guidelines C.121 (pure abstract interface), C.35/C.127 (virtual destructor), NVI pattern where needed

**Avoids:** Pitfall #2 (IPacker used at coarse granularity — `packFilesToZip`, not per-file virtual dispatch), Over-mocking anti-pattern (STACK.md: only mock external side effects, not value types or standard library)

**Research needed:** Phase 3 design review — IPacker method granularity. Too fine (per `addFile` virtual call) = hot-path virtual dispatch. Too coarse (one `execute(plan)` method) = mock provides zero verification value. The recommended granularity: `packFilesToZip(entries, zipPath, callbacks)` per archive — verified in practice by STACK.md's hot-path analysis.

### Phase 4: Consumer Migration & Cleanup
**Rationale:** With the OO API stable and testable, migrate consumers one subsystem at a time. Each migration is a small, reviewable PR that changes one consumer to use `PackService`/`Packer` directly instead of the facade. After all consumers migrate, remove the facade and deprecated wrappers. The incremental approach means any migration regression is caught immediately by the test suite.

**Delivers:**
- `video_process.cpp` migrated: uses `PackService` + `Packer` directly
- `picture_process.cpp` migrated (2 PackPlan sites)
- `archive_plan.cpp` migrated: uses `PackService::selectPackPlanIndexes`, `PackPlan` accessors
- `app/pipeline.cpp` migrated
- `video_output_planning.cpp` trivial update
- `pack_facade.h` removed; `[[deprecated]]` wrappers removed from pack_service.h, packer.h
- Final verification: all 909 assertions pass, `git diff src/video/ src/picture/` is minimal and contains only API migrations (not leakages)

**Uses:** Facade pattern from ARCHITECTURE.md (Phase 3) — has been providing backward compat throughout Phases 2-3; now removed

**Avoids:** Pitfall #6 (consumers migrated last, not first), Pitfall #4 (test breakage — tests were updated incrementally in Phases 2-3)

### Phase Ordering Rationale

- **Phase 1 first** because it's zero-risk pure refactoring. Breaking the circular dependency and cleaning up the global namespace creates a clean foundation. If Phase 1 breaks anything, it's purely an include-path problem — no behavioral risk.
- **Phase 2 before Phase 3** because the service classes must exist and be verified correct before adding abstraction (IPacker). If the service classes are buggy, mocking them in Phase 3 creates false confidence. Validate with real libzippp first, then add mock boundary.
- **Phase 3 before Phase 4** because new unit tests with MockPacker provide a safety net before touching consumer code. When Phase 4 migration breaks a consumer, the failure is caught by both the new unit tests and the existing integration tests.
- **Phase 4 last** because consumer migration is the riskiest change (touches 4+ subsystems). Having all infrastructure tested and stable before touching consumers minimizes the blast radius of any mistake.
- **At every step, all 909 assertions pass.** No "won't compile until Phase N" situations. Each PR is independently buildable and testable.

### Research Flags

**Phases that need `/gsd-research-phase` during planning:**

- **Phase 2 (Service Class Extraction):** The exact division of responsibility between `PackService` and `Packer` needs field research in the codebase. Which anonymous-namespace functions go to which class? How does `CompactProgressState` interact with both? The STACK.md classification table (lines 117-124) provides a starting taxonomy, but the 30+ individual functions need auditing. Also: the callback extraction from PackPlan to PackService needs API design research — `PackProgressCallbacks` struct vs. constructor parameters vs. setter injection.

- **Phase 3 (Dependency Injection):** IPacker method granularity needs benchmarking validation. The coarse-grained `packFilesToZip` is recommended, but if `groupFilesBySize` is also on IPacker, how does it compose with zip operations? The test mock design (MockPacker) needs to capture enough detail to verify correct behavior without becoming a test framework.

**Phases with standard patterns (skip research-phase):**

- **Phase 1 (Type Extraction):** Well-documented mechanical refactoring. The pattern (`pack_types.h` extraction, namespace migration) is standard C++ module organization. The dependency graph is fully known from the codebase.

- **Phase 4 (Consumer Migration):** Well-understood API migration. Each consumer changes ~50 lines from free-function calls to class methods. The facade pattern ensures no atomically-large changes. Standard migration technique.

## PackPlan Conflict Resolution

### The Conflict

Four researchers reached contradictory conclusions about PackPlan:

| Researcher | Recommendation | Primary Justification |
|------------|---------------|----------------------|
| STACK.md | Class with private data + Config struct factory | "PackPlan has coupled invariants (groups ↔ outputDir)" |
| FEATURES.md | Class with Builder pattern (P1 must-have) | "Designated initializers are gone after encapsulation" |
| ARCHITECTURE.md | Class with PackPlan::Builder struct | "Encapsulation prevents accidental mutation of callbacks" |
| PITFALLS.md | **MUST remain aggregate struct** | 16 construction sites, static_assert guard, DTO pattern |

### Resolution

**PackPlan remains an aggregate struct.** PITFALLS.md's evidence is dispositive:

1. **Concrete constraint:** `static_assert(std::is_aggregate_v<pack::PackPlan>)` at `pack_service.h:52` is a compile-time guard. Removing it requires migrating 16 construction sites simultaneously — a "100% build break with zero incremental recovery path."

2. **Idiomatic C++:** C++ Core Guidelines C.2 states: *"Use class if the class has an invariant; use struct if the data members can vary independently."* PackPlan's data members vary independently — `groups` and `outputDir` and `compact` are independently settable by different construction sites. The "invariants" (groups non-empty) are pre-conditions of operations, not of the data structure itself. Validation belongs in factory functions, not in the struct.

3. **PITFALLS's alternative is architecturally superior:** Encapsulate the *logic around* PackPlan (move `packGroupsCompact`, `packGroupsFull`, `resolveZipNameForIndex`, etc. into `PackService`) rather than encapsulating PackPlan's data. This achieves the OO goals — cohesive classes with behavioral methods — without breaking the data contract that 16 consumers and 215 test cases depend on.

4. **STACK.md, FEATURES.md, and ARCHITECTURE.md understate the cost:** They acknowledge the `static_assert` as a constraint but propose solutions (Builder, facade) that address *construction* without addressing the *aggregate-ness requirement*. Even C++26 does not support designated initializers on non-aggregates. A `PackPlan::Builder` struct passed to a constructor is valid C++, but the resulting `PackPlan` is no longer an aggregate — the `static_assert` fails.

### What This Means for v1.3

The milestone goal "struct → class conversion" should be interpreted as:

- **Do:** Free functions → class methods (PackService, Packer)
- **Do:** Anonymous-namespace helpers → private methods
- **Do:** Progress callbacks → injectable struct
- **Do:** TU-local state (CompactProgressState) → private class members
- **Do NOT:** Make PackPlan's data private
- **Do NOT:** Remove the `static_assert(is_aggregate_v<PackPlan>)`
- **Do NOT:** Replace designated initializers with builder/constructor at call sites

The progress callback extraction (moving 6 `std::function` members off PackPlan into PackService or a `PackProgressCallbacks` struct) is still achievable: callbacks that are currently set on PackPlan at construction time can instead be passed to PackService's `runPackPlan()` method or set via constructor injection on PackService. PackPlan goes from 15 fields to ~9 (callbacks removed, data fields remain public).

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Backed by C++ Core Guidelines (isocpp), cppreference.com, and direct codebase analysis. Core patterns (constructor injection, struct-vs-class, final, no-deep-hierarchy) are unanimously endorsed by all four researchers. |
| Features | HIGH | Derived from PROJECT.md milestone definition and direct code audit. Feature dependencies and prioritization are internally consistent and validated against the existing 909-assertion test suite. |
| Architecture | HIGH | Target architecture diagrammed from actual file-by-file analysis of pack_service.h/.cpp, packer.h/.cpp, and all 6 consumer sites. Component boundaries, build order, and mockability strategy are all grounded in real code, not abstraction. |
| Pitfalls | HIGH | Pitfalls are codebase-specific, verifiable by `grep` and `static_assert`. Pitfall #1's 16-construction-site count is exact and audit-traced. Pitfall #2's LTO analysis references `xmake.lua:3` directly. Recovery strategies are tested against plausible failure modes. |

**Overall confidence:** HIGH — All four research files independently sourced the same C++ Core Guidelines and the same codebase files. There are no speculative findings.

### Gaps to Address

- **Callback extraction boundary:** Which of PackPlan's 6 `std::function` callbacks move to PackService vs. stay on PackPlan? The `onGroupStart`/`onGroupSuccess`/`onGroupFailure` callbacks are progress-reporting and belong in PackService. But `zipNameForIndex` and `progressLabelForIndex` are naming strategies tightly coupled to the PackPlan's data (groups). Resolve during Phase 2 planning — use the classification from FEATURES.md's dependency analysis.

- **IPacker granularity benchmark:** The recommendation is one virtual call per archive (`packFilesToZip`), not per file. But `groupFilesBySize` and `groupPackFiles` are pure-computation grouping functions — they don't need to be on IPacker at all. Confirm during Phase 3 design review whether IPacker should contain only zip I/O methods (not grouping). This keeps the interface minimal and keeps virtual dispatch off algorithmic paths.

- **xmake build system impact:** The `src/**.cpp` wildcard auto-includes new files. Adding `pack_plan.cpp`, `packer_interface.cpp`, and possibly a `mock_packer.cpp` adds TUs. Measure baseline compilation time before Phase 1 to detect regressions. The PITFALLS.md compilation-time trap (Pitfall #7) provides the benchmark methodology.

- **`archive_plan.cpp` coupling:** This core module accesses `plan.groups[index]` directly. If PackPlan's `groups` field were ever made private (explicitly NOT recommended), archive_plan would need a `PackPlan::groupAt(size_t)` accessor. This is a concrete example of why PackPlan should stay as an aggregate — `archive_plan.cpp` is a legitimate consumer of pack plan structure, not an accidental coupling to be severed.

## Sources

### Primary (HIGH confidence)
- **`/isocpp/cppcoreguidelines`** (Context7) — C.2 (struct vs class), C.134 (public vs private data), C.121 (pure abstract interfaces), C.35/C.127 (virtual destructors), C.133 (protected data), C.138 (final), C.4 (method vs free function), NVI pattern, PIMPL idiom. Referenced by all four researchers.
- **`src/pack/pack_service.h`** — PackPlan struct, `static_assert(is_aggregate_v<PackPlan>)` at line 52, free function declarations. Primary architectural source.
- **`src/pack/pack_service.cpp`** — 455 lines of anonymous-namespace helpers, PackPlan orchestration, CompactProgressState. Primary refactoring target.
- **`src/pack/packer.h`** — Global-scope structs, free function declarations, circular include of pack_service.h. Primary dependency-resolution target.
- **`src/pack/packer.cpp`** — libzippp integration, file grouping algorithms, 3 overloads of packFilesToZip.
- **`src/video/video_process.cpp:395-448`** — PackPlan construction site #1 (production)
- **`src/picture/picture_process.cpp:150-198, 607-615`** — PackPlan construction sites #2-3 (production)
- **`src/core/archive_plan.cpp:26-89`** — PackPlan consumer in core module
- **`tests/pack_service_tests.cpp`** — 10 PackPlan designated-initializer test sites
- **`xmake.lua:3, 38-76`** — LTO policy, glob-based build, test linking
- **`.planning/PROJECT.md:27-36, 90-98`** — v1.3 milestone goals and architectural constraints
- **cppreference.com** — Modules compiler support status, C++20/23/26 feature availability

### Secondary (MEDIUM confidence)
- **`/refactoringguru/design-patterns-cpp`** (Context7) — Strategy, Adapter, Facade patterns with C++ examples
- **`src/core/job_state.h:72-129`** — `jobstate::Store` as existing OO class precedent (constructor DI, private data, public methods)

### Tertiary (LOW confidence — needs validation)
- (None — all findings are grounded in direct codebase analysis or official C++ guidelines)

---

*Research completed: 2026-04-29*
*Ready for roadmap: yes*
*Cross-researcher conflict resolved: PackPlan remains aggregate (PITFALLS position confirmed)*
