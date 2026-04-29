# Pitfalls Research

**Domain:** C++ procedural-to-OO refactoring (pack subsystem)
**Researched:** 2026-04-29
**Confidence:** HIGH

## Critical Pitfalls

Mistakes that cause rewrites, test cascades, or silent behavioral changes in this specific codebase.

---

### Pitfall 1: Breaking the designated-initializer contract on PackPlan

**What goes wrong:**
Converting `PackPlan` from `struct` to `class` with private members immediately breaks the `static_assert(std::is_aggregate_v<pack::PackPlan>)` guard at `pack_service.h:52`. Every designated-initializer construction site — **six in production code** (video_process.cpp:423, picture_process.cpp:474/607, packer.cpp:815, pack_service.cpp:402, archive_plan.cpp:65) and **ten in test code** — fails to compile. This is not just a test failure; it's a 100% build break with zero incremental recovery path.

**Why it happens:**
PackPlan currently relies on C++ aggregate initialization with designated initializers (`.groups = ...`, `.outputDir = ...`). This is a deliberate design choice enforced by static_assert. Even C++26 does not support designated initializers on non-aggregates. Naïvely adding `private:` to PackPlan deletes the aggregate property, causing every consumer — including video, picture, and all 215 test cases — to fail simultaneously.

**How to avoid:**
1. **Do NOT make PackPlan a class with private data.** PackPlan is a data-transfer object (DTO) with callback hooks — it is correctly modeled as an aggregate struct.
2. **Encapsulate the logic around PackPlan, not PackPlan itself.** Move the anonymous-namespace free functions (`packGroupsCompact`, `packGroupsFull`, `resolveZipNameForIndex`, etc.) into a `PackService` class that *takes* a `PackPlan const&` but does not own it.
3. **If PackPlan MUST change**, transition through a builder pattern that preserves the aggregate while adding validation, then change consumers one at a time. But this is high-risk and unnecessary for v1.3.

**Warning signs:**
- Seeing `private:` in a header that currently has a `static_assert(std::is_aggregate_v<...>)`.
- Grepping for `PackPlan{` and seeing 20+ designated-initializer sites.
- Build errors: `"cannot use designated initializer with non-aggregate type"`.

**Phase to address:**
Phase 0 (planning) — Must be in the research/audit stage before any code change. Design review must confirm whether PackPlan stays aggregate or transitions with a full impact analysis.

---

### Pitfall 2: Virtual dispatch on the hot pack path

**What goes wrong:**
Introducing virtual functions (`virtual`, `override`) on classes that sit in the critical packing path — `packFilesToZip` is called for every file in every archive. A virtual call through the vtable prevents inlining, adds an indirect branch, and on clang-cl with LTO enabled (`xmake.lua:3`), can prevent cross-TU optimization. This is easily a 10–50% throughput regression on large batches.

**Why it happens:**
Developers trained in "classical OO" reflexively add `virtual` to methods they think might need polymorphism. In a pack subsystem where the only polymorphism is compile-time (compact vs. full progress mode selected via `if (plan.compact)`), there is zero need for runtime dispatch.

**How to avoid:**
1. **Zero virtual functions in v1.3.** The refactoring goal is encapsulation, not polymorphism. Use `final` on any class that could accidentally become a base class.
2. **Mark all pack classes `final`** — `class PackService final { ... };` — this allows the compiler to devirtualize any accidentally-virtual calls and signals intent.
3. **Prefer `std::variant` or enum-based dispatch over inheritance** for any behavioral variation (but this codebase already uses a boolean `compact` flag correctly).
4. **Benchmark before and after.** Run `xmake build` and time a pack of 1000 files. Any regression >2% is a blocking issue.

**Warning signs:**
- `virtual` keyword appearing in pack headers.
- `override` keyword.
- Virtual destructors in classes without virtual functions.
- Clang-cl warning `-Wnon-virtual-dtor`.

**Phase to address:**
All phases — must be checked at every code review. Add a `grep -r "virtual" src/pack/` to the pre-commit hook for v1.3.

---

### Pitfall 3: Circular include dependency between pack and core

**What goes wrong:**
`pack_service.h:3` includes `core/app_context.h`. If the new `PackService` class adds `core/task_executor.h` or `core/progress.h` (needed for its methods), and those headers transitively depend on pack, you get a circular include. At best, `#pragma once` silently masks it. At worst, incomplete types cause cryptic template instantiation errors that take hours to diagnose.

**Why it happens:**
The current codebase avoids this by keeping pack headers thin (only structs and function declarations) and including heavy dependencies (task_executor.h, packer.h) only in `.cpp` files. When you move function implementations into header methods (e.g., making `packGroups` a member of `PackService`), the heavy includes migrate to the header.

**How to avoid:**
1. **Keep headers implementation-free.** Public method *declarations* in the header. Method *definitions* in the `.cpp` file. This is the existing pattern and must be preserved.
2. **Use forward declarations** for types only needed in method signatures. E.g., `namespace taskexec { struct TaskPlan; }` in the header.
3. **If a method signature requires a type from another module**, consider whether the method belongs in that module's header or whether the dependency can be inverted (dependency inversion).
4. **Validate with a compile-time check:** After each header change, run `xmake build -j1` and verify no new include cycles appear. Use clang's `-H` flag to trace includes if suspicious.

**Warning signs:**
- Adding `#include "core/task_executor.h"` to `pack_service.h`.
- `incomplete type` errors in template instantiation backtraces.
- Compilation of a single `.cpp` file pulling in 50+ headers.

**Phase to address:**
Phase 1 (core class extraction) — Every header modification must be verified with a clean single-thread build (`xmake build -j1`).

---

### Pitfall 4: Test breakage from access modifier change on functions

**What goes wrong:**
When free functions in anonymous namespaces (`pack_service.cpp:21-323`, `packer.cpp:32-354`) are moved into class `private:` sections, they become inaccessible to tests. Tests that currently call `pack::packGroups(plan)` will still work (that's public API), but any test that constructs internal state objects (like `CompactProgressState` at pack_service.cpp:71) or calls helper functions cannot. More critically, tests pass through `pack::packGroups` → `packGroupsCompact` / `packGroupsFull` — if these become private methods of `PackService`, the test can't invoke them separately.

**Why it happens:**
The anonymous namespace members are currently TU-scoped but accessible to any function in the same `.cpp`. When moved into a class, they get the class's access level. `CompactProgressState` and `packGroupsCompact`/`packGroupsFull` contain the actual packing logic — tests exercise them through `packGroups(plan)`, but unit tests might need finer-grained access.

**How to avoid:**
1. **Map what tests actually need.** All 10 `pack::PackPlan{...}` construction sites in tests pass through the public API (`pack::packGroups`, `pack::selectPackPlanIndexes`, `pack::buildGroupOrdinalRanges`). These are the correct public surface.
2. **Keep the existing public API surface intact.** The current free functions in `namespace pack` (not anonymous) are the public API. These must remain callable with identical signatures.
3. **If a helper needs test access**, make it a `protected` method with a test-only subclass, or keep it as a free function in a `detail` namespace (preferred: avoids test-only code in production headers).
4. **Add one integration test that exercises the full `packGroups` path** before any refactoring, to serve as a canary.

**Warning signs:**
- `private:` section containing functions that were previously in the anonymous namespace.
- Tests failing to link because a symbol went from TU-local to class-private.
- Adding `friend class PackServiceTest;` to production code (this is a code smell).

**Phase to address:**
Phase 1 (initial class extraction) — The first class refactored must pass all existing tests without modification to test assertions.

---

### Pitfall 5: Getter/setter proliferation for every field

**What goes wrong:**
Converting every public struct field to `private` + `get_x()`/`set_x()` pair. The C++ Core Guidelines explicitly call this an anti-pattern: *"Trivial getters and setters: class with verbose accessors — bad. struct with public members — good."* For PackPlan's 11 fields, this means 22 accessor functions that serve no purpose except ceremony.

**Why it happens:**
Misunderstanding encapsulation. Encapsulation means hiding *implementation details* behind *behavioral interfaces*. If the getter just returns the field and the setter just assigns it, you haven't hidden anything — you've just added indirection and made designated-initializer construction impossible. The C++ Core Guidelines state: *"If a class has no behavioral member functions, use a struct."*

**How to avoid:**
1. **Distinguish data from behavior.** PackPlan, PackFileEntry, PackRunResult, FileOrdinalRange, PackGroupInput, PackGroupPartition, PackEntryInput, PackEntryPartition — these are data bags. Keep them as structs (or `class` with `public:` for consistency, but maintain aggregate-ness).
2. **Encapsulate behavior, not data.** The OO value is in wrapping the 30+ free functions in pack_service.cpp/packer.cpp into cohesive classes with clear responsibilities. The data types are the *interface* between those classes and consumers.
3. **Only add accessors when there's an invariant.** If you were to add a validation (e.g., `setMaxParallelJobs` that clamps to [1, N]), that's a legitimate encapsulation. But v1.3's goal statement says "zero behavioral change," so validation changes are out of scope.

**Warning signs:**
- Class with more `get`/`set` methods than behavioral methods.
- `int get_x() const { return x_; }` — this is exactly the anti-pattern.
- Accessor that returns a const reference to a member without any computation or invariant check.

**Phase to address:**
Phase 0 (design review) — Class design must distinguish data-transfer types (stay structs) from service types (become classes).

---

### Pitfall 6: Migration order — converting PackPlan before its consumers

**What goes wrong:**
PackPlan is consumed by video_process.cpp, picture_process.cpp, packer.cpp, archive_plan.cpp, and all tests. If you start the refactoring by changing PackPlan, every consumer must change simultaneously. This violates the incremental-refactoring principle and guarantees a long period where nothing compiles.

**Why it happens:**
"Start with the core" is intuitive but wrong for data-transfer types. PackPlan is a dependency, not a dependency root. Changing it first is like replacing the foundation while people are living in the house.

**How to avoid:**
1. **Refactoring order: leaves → trunk, not trunk → leaves.** Start with internal types that have no consumers outside their TU:
   - `CompactProgressState` (pack_service.cpp:71) — TU-local, pure implementation
   - `PreparedPackEntry`/`PreparedPackChunk` (packer.cpp:78-89) — TU-local
   - Anonymous-namespace grouping functions in packer.cpp
2. **Then service classes** — `PackService` wrapping the public free functions, taking PackPlan by const reference
3. **PackPlan stays aggregate** unless there's a compelling reason to change it (and there isn't for v1.3)
4. **Each step must compile and pass all 909 assertions**

**Warning signs:**
- First PR touches `pack_service.h` and 6 other files.
- "This won't compile until Part 3" — any multi-PR dependency that can't compile independently.
- Changing a type that has a `static_assert` guarding its current properties.

**Phase to address:**
Phase 1 — Must define clear "leaf first" ordering. Each PR must be independently buildable and test-passing.

---

### Pitfall 7: Compilation time regression from header bloat

**What goes wrong:**
The current codebase has fast compilation because pack headers are lean: `pack_service.h` (82 lines, 7 includes), `packer.h` (126 lines, 10 includes). When free functions become class methods defined in headers (inline or templated), every consumer of the header must recompile those definitions. Even worse, if the header pulls in transitive includes (task_executor, progress, job_state), compilation time can easily double.

**Why it happens:**
OO refactoring encourages putting methods in headers for inlining or because templates are header-only. But this codebase has no pack templates that require header definitions. The LTO in xmake.lua (`set_policy("build.optimization.lto", true)`) already enables cross-TU inlining — there is zero benefit to header-based method definitions.

**How to avoid:**
1. **All method definitions in .cpp files.** No exceptions for v1.3.
2. **Measure compilation time before and after each phase.** `xmake build -j1 --verbose 2>&1 | grep "compile"` — any TU going from <1s to >2s needs investigation.
3. **Use `final` on classes** to enable devirtualization without LTO requirements.
4. **The xmake.lua uses `add_files("src/**.cpp")`** which auto-includes new files — adding a `pack_service_impl.cpp` would be auto-picked but adds a TU. Keep the TU count close to current.

**Warning signs:**
- Method bodies appearing in `.h` files.
- `inline` keyword in pack headers.
- `constexpr` methods defined in headers that could be in .cpp.
- Increase in `#include` count in any pack header.

**Phase to address:**
All phases — Add a CI check: `grep -c "^\s*(virtual|inline|constexpr).*{" src/pack/*.h` must return 0.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| `friend class TestAccessor;` | Quick test access to private members | Production code now knows about test code; test-specific code in headers slows compilation | Never — use public API or `detail` namespace free functions |
| `public:` section with all old struct fields | Builds compile, all tests pass | Zero encapsulation — the refactoring is cosmetic and adds zero value | Never for v1.3 — either encapsulate or don't change |
| Duplicate free function bodies in member methods + keep originals | "Incremental" — old code still works | Two implementations to maintain; bugs fixed in one but not the other | Only during a single-PR transition where the old is deleted in the same PR |
| `#include` everything in a single `pack_all.h` | Convenience for consumers | Loss of incremental compilation; every consumer recompiles on any pack change | Never — the current per-header include pattern is correct |
| Abstract base class for PackService with single implementation | "Extensibility" | Virtual call overhead on packing hot path; complexity for zero benefit | Only when a second implementation exists and is being merged simultaneously |
| `std::shared_ptr` for all objects instead of value semantics | No lifetime management worries | Heap allocation overhead; cache-unfriendly indirection | Only for data shared across threads (like `CompactProgressState` which already uses atomic members correctly) |

---

## Integration Gotchas

Common mistakes when pack subsystem connects to video/picture consumers.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| **video_process.cpp builds PackPlan** (line 423) | Changing PackPlan constructors breaks this site — video team may not know pack was refactored | PackPlan must maintain backward-compatible construction. If builder is added, keep the aggregate constructor working during transition |
| **picture_process.cpp builds PackPlan** (lines 474, 607) | Same as above, plus picture uses `groupPackEntriesWithSubparts` from packer.h — if packer functions move into a class, picture must update | Keep the existing free-function signatures as public API during transition; deprecate only after consumers migrate |
| **archive_plan.cpp uses `pack::selectPackPlanIndexes`** (line 65) and accesses `plan.groups[i]` | Making `groups` private breaks archive_plan's direct access | `groups` is a data member of a DTO — it should remain publicly accessible. archive_plan is a core module that legitimately needs this data |
| **All consumers construct `pack::PackFileEntry{ .sourcePath = ..., .zipEntryName = ... }`** (video_process.cpp:414, picture_process.cpp:243+) | Making PackFileEntry fields private breaks designated-initializer construction everywhere | PackFileEntry is a value type with `operator==` defaulted — keep it as an aggregate |
| **video_output_planning.cpp calls `groupPackFiles` from packer.h** (line 183) | If `groupPackFiles` becomes a method, video must construct the class first | Keep `groupPackFiles` as a free function or provide it as a static method on the grouping class |
| **picture_process.cpp calls `groupPackEntriesWithSubparts`** (line 577) | Same pattern — packer grouping functions are called from outside the pack subsystem | These are genuinely reusable algorithms. They should remain callable without constructing a service object |

---

## Performance Traps

Patterns that work at small scale but fail with real workloads.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Virtual dispatch on `addFile` loop | Per-file overhead × 1000s of files; `packFilesToZip` becomes CPU-bound on vtable lookup instead of I/O | Zero virtual in pack hot paths; mark all pack classes `final` | 1000+ files, noticeable at 10k files |
| Heap-allocating `PackService` per `packGroups` call | `packGroups` is called once per workflow; heap allocation of a stateless service object adds ~100ns — negligible. But if it happens inside the per-file loop, catastrophic | `PackService` should be stack-allocated or have static lifetime; no `new`/`make_unique` inside loops | Inside per-file loop: breaks at 100 files |
| `std::function` copy overhead for callbacks | `PackPlan` has 7 `std::function` members. Copying a PackPlan copies all callbacks — but PackPlan is passed as `const&` everywhere so this is already avoided | Do NOT change `PackPlan const&` to `PackPlan` (by-value) in any function signature | Would break immediately on large callback captures (picture's `PicturePackNamingState` shared_ptr) |
| Thread contention on mutex in `CompactProgressState` | `state.mutex` is locked per file completion. At 10k files with 8 concurrent archives, negligible. But moving to a coarser lock (making the whole PackService mutex-protected) would serialize packing | Keep the fine-grained locking; do not add a "convenience" mutex on the service class | Breaks at 4+ concurrent archives with small files (I/O < lock contention) |

---

## Security Mistakes

Beyond general C++ security — domain-specific to this codebase.

| Mistake | Risk | Prevention |
|---------|------|------------|
| Exposing `std::function` setters without validation | Malformed callback could throw from inside `runTasks` worker thread, causing std::terminate | All callbacks in PackPlan are set by trusted internal code; if a setter is added, wrap in `noexcept` or document the noexcept requirement |
| Path traversal via crafted `zipEntryName` | `zipEntryName` like `../../etc/passwd` could extract outside target directory | `normalizeZipEntryName` (packer.cpp:44) already strips leading `/` but does not prevent `../` — this is pre-existing and NOT a v1.3 concern, but adding OO wrappers must not weaken existing checks |
| `src/**.cpp` wildcard in xmake.lua adding all files | Adding a `src/pack/exploit_helper.cpp` would be compiled and linked | Pre-existing build config; v1.3 must not introduce files outside `src/pack/` |

---

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **"PackService class extracted":** Are all 30+ anonymous-namespace functions accounted for? Verify: `grep -c "namespace {" src/pack/*.cpp` should equal zero after complete migration (or only contain new TU-local helpers with justification)
- [ ] **"All tests pass":** Are there *new* tests for the OO interface? The 909 existing assertions test behavior through the free-function API. If that API is now a thin delegate to methods, there should be at least 5 new tests that exercise the class directly — verifying construction, destruction, and method chaining. Without these, the OO layer is untested.
- [ ] **"Encapsulation complete":** Can a consumer access pack internals without going through the public interface? Verify: try to include only the public header and call internal methods — it should fail to compile.
- [ ] **"No header bloat":** Compare `wc -l src/pack/*.h` before and after. Header size should be within 20% of original. If headers grew 2x+, the OO refactoring moved implementation into headers.
- [ ] **"LTO still works":** The `xmake.lua` LTO policy requires that link-time optimization can see through method boundaries. Verify: `xmake f -m release && xmake build` — if linker errors about undefined symbols appear, LTO is conflicting with new OO boundaries.
- [ ] **"Video/picture consumers unchanged":** `git diff src/video/ src/picture/` should show zero changes (except possibly `#include` path adjustments). If video or picture files change, the encapsulation boundary leaked.

---

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Broke designated-initializer by making PackPlan non-aggregate | LOW | Revert PackPlan to aggregate struct. The work wasn't wasted — extract the logic into a service class instead. 1 PR to fix. |
| Added virtual function discovered by benchmark regression | LOW | Remove virtual, add `final`. Re-measure. If regression persists, the problem is elsewhere — git bisect. |
| Circular include (doesn't compile) | MEDIUM | Forward-declare the type that caused the cycle. Move the method definition to .cpp. If the method must be inline, extract a free function in a detail header. |
| Test cascades (50+ test failures) | MEDIUM | Revert to last green commit. Re-apply changes in smaller increments (one class/method at a time). Each increment must pass all tests. |
| Compilation time doubled | MEDIUM | Run `clang-cl -H` to see include tree. Move method bodies from .h to .cpp. Remove unnecessary includes from headers. Add forward declarations. |
| Over-engineered (abstract base, deep hierarchy, getters everywhere) | HIGH | Full revert and restart. Over-engineering in C++ OO creates design debt that's harder to unwind than procedural debt. The correct approach: concrete classes, flat hierarchy, behavioral methods only. |

---

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Breaking PackPlan aggregate contract | Phase 0 (design review) | `static_assert(std::is_aggregate_v<pack::PackPlan>)` still holds after Phase 1 |
| Virtual dispatch on hot path | All phases | `grep -r "virtual" src/pack/` returns empty |
| Circular includes | Phase 1 (initial class extraction) | `xmake build -j1` clean build succeeds |
| Test breakage from access modifier change | Phase 1 | All 909 assertions pass; `git diff tests/` is empty |
| Getter/setter proliferation | Phase 0 (design review) | Code review: count behavioral methods vs accessors on each new class |
| Wrong migration order | Phase 0 (planning) | First PR touches only TU-local internals, not pack_service.h |
| Compilation time regression | Phase 1, 2, 3 | `xmake build -j1` time within 120% of baseline |
| Integration breakage with video/picture | Phase 2 (service class extraction) | `git diff src/video/ src/picture/` is empty |
| Header bloat from inlined methods | All phases | `wc -l src/pack/*.h` within 120% of baseline |
| Thread safety regression from coarser locking | Phase 2 | Per-file progress still resolves correctly under concurrent packing |

---

## Sources

- **C++ Core Guidelines** (isocpp/cppcoreguidelines) via Context7 — authoritative source for class design, interface segregation, avoiding trivial getters/setters, preferring concrete types. [HIGH confidence]
- **isocpp.org Super-FAQ** — Classes and Objects section — defines encapsulation, interface quality, struct vs class. [HIGH confidence]
- **Codebase analysis** — All pack subsystem files (pack_service.h/.cpp, packer.h/.cpp), consumer files (video_process.cpp, picture_process.cpp, archive_plan.cpp, video_output_planning.cpp), test files, xmake.lua build configuration. [HIGH confidence — direct code inspection]
- **xmake.lua:3** — LTO policy (`set_policy("build.optimization.lto", true)`) confirms cross-TU optimization is active, making header-based inlining redundant. [HIGH confidence]
- **pack_service.h:52-55** — `static_assert(std::is_aggregate_v<pack::PackPlan>)` confirms PackPlan must remain aggregate. [HIGH confidence]

---

*Pitfalls research for: encro pack subsystem C++ OO refactoring (v1.3)*
*Researched: 2026-04-29*
