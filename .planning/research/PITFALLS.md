# Domain Pitfalls

**Domain:** C++26 Tech Debt Resolution — encro
**Researched:** 2026-04-28

---

## Critical Pitfalls

Mistakes that cause rewrites or major issues.

### Pitfall 1: Deleting the Wrong Duplicate Test (DEBT-02)

**What goes wrong:** Two tests at lines 98 and 132 of `pack_service_tests.cpp` both assert `compact` preservation through `selectPackPlanIndexes`, but the test at line 132 also validates `zipNameForIndex` and `progressLabelForIndex` remapping — the true purpose of that test case. Removing the line 132 test entirely would lose coverage of the v1.1 refactoring that extracted `makeSubsetZipNameResolver` and `makeSubsetProgressLabelResolver`.

**Why it happens:** The tests appear similar superficially because both check `result.compact == true`, but they validate different concerns:
- **Line 98** (`selectPackPlanIndexes preserves compact from source plan`): A **property test** — verifies `compact` field propagation for both `true` and `false` values through the index remapping logic. No other fields are tested.
- **Line 132** (`selectPackPlanIndexes delegates to named helpers instead of lambda-wrapping-lambda`): An **integration test** — verifies the v1.1 refactoring correctly delegates to `makeSubsetZipNameResolver` and `makeSubsetProgressLabelResolver` factory functions. The `compact` check at line 161 is incidental coverage.

**Consequences:** Deleting the test at line 132 loses regression protection for the v1.1 factory function refactoring. Deleting the test at line 98 loses the `compact=false` coverage. Neither test is truly "duplicate" in content.

**Prevention:** The actual DEBT-02 task is not to delete a test case but to **consolidate** the coverage — the compact-field check in line 161 is redundant with line 129, but the zipNameForIndex/progressLabelForIndex assertions are unique. The correct action: keep the line 132 test, remove the **redundant compact assertion** (line 161) from it, and ensure the line 98 test covers both `compact=true` and `compact=false`. Do NOT delete entire test cases.

**Detection:** Before modifying, run `grep -n "compact" tests/pack_service_tests.cpp` to count all 19 `compact` references and verify each test's distinct purpose. The assertion count must remain at 910 — any drop is a red flag.

### Pitfall 2: Silent Behavioral Change from Explicit Struct Initialization (DEBT-01)

**What goes wrong:** Adding `.compact = true` to `picture_process.cpp:467`'s `PackPlan` designated initializer list could change behavior if the default member initializer (`bool compact = true` in `pack_service.h:48`) is ever changed, OR if the current code accidentally omits other fields and adding `.compact` introduces a different initialization order.

**Why it happens:** In C++20/26 designated initializers (per cppreference aggregate initialization rules):
- Members **not mentioned** in a designated initializer list get their default member initializer if one exists, else are copy-initialized from `{}`.
- `PackPlan::compact` has `bool compact = true` as its default — so omitting `.compact` from every designated initializer list yields `compact = true`.
- **Adding `.compact = true` to a designated initializer list that already omits it produces IDENTICAL behavior today** — but makes the intent explicit and prevents silent breakage if someone later changes the default to `false`.

**Consequences of getting it wrong:**
- **Too conservative (omit the fix):** Future default change silently breaks the compress-picture path without compiler diagnostic.  
- **Wrong field added:** Accidentally adding `.compact = true` to the wrong struct (e.g., `PicturePackNamingState` on line 465, which has no `compact` field) → compilation error.  
- **Designator order violation:** C++ requires designated initializers to appear in declaration order. Since `compact` is the LAST field in `PackPlan`, adding it won't cause order issues.  
- **Compiler warning:** clang-cl with `-Wmissing-field-initializers` may warn about uninitialized fields even when default member initializers exist. This is a cosmetic warning, not a bug.

**Prevention:**
1. **Verify `PackPlan` field order** (lines 36-49 in `pack_service.h`): `groups` → `outputDir` → `zipNameForIndex` → `progressLabelForIndex` → `onGroupStart` → `onGroupSuccess` → `onGroupFailure` → `onCompactProgress` → `onCompactStatusText` → `maxParallelJobs` → `removeOnFailure` → `compact`.
2. **Place `.compact = true` as the last field** in the designated initializer (after `.removeOnFailure = true` and `.maxParallelJobs`).
3. **Build and run full test suite** — any behavioral change in struct padding or initialization order will manifest as a test failure or assertion change.
4. **Use `static_assert`** on `PackPlan` to verify it remains an aggregate type: `static_assert(std::is_aggregate_v<pack::PackPlan>)`.

**Detection:** Diff the test output before/after. All 910 assertions must pass identically. The compact-mode tests (`packGroups compact mode reports per-file progress updates`, etc.) are the canary — if the compress-picture path behavior changed, these tests would fail or reveal different assertion counts.

### Pitfall 3: ODR Violations When Refactoring Template Functions (OPTIM-01)

**What goes wrong:** `withJobState` and `withActionJobState` are `template<class Fn> inline` functions defined in `video_workflow_utils.h`. Any refactoring that moves them out of the header, changes their `inline` or template linkage, or creates a separate instantiation unit risks **One Definition Rule (ODR) violations**.

**Why it happens:** Per the C++ standard (cppreference ODR rules):
- Template functions and inline functions are **exempt from the single-definition requirement** IF every definition across all translation units is identical in token sequence and meaning.
- These functions are currently used in 2 translation units: `video_batch_execution.cpp` (8 call sites) and `video_process.cpp` (3 call sites).
- Moving either function out of `video_workflow_utils.h` into a `.cpp` file would break all other translation units that include the header — the definition would no longer be visible at instantiation points.
- If the refactoring involves splitting these into different namespaces or adding a wrapper layer, the `using videoworkflow::withJobState;` declarations at file scope in the consuming `.cpp` files would silently resolve to different instantiations.

**Specific risks with this codebase:**
1. **`withActionJobState` depends on `withJobState`** (line 48 of `video_workflow_utils.h`). If only one is refactored, the dependency chain must be preserved identically.
2. **Clang-cl's template merging:** Clang merges identical template instantiations at link time. If two TUs instantiate slightly different versions (e.g., one with `const&` vs `&&` on `Fn`), the linker picks one arbitrarily → undefined behavior.
3. **Lambda uniqueness:** The `Fn&&` template parameter deduces to a unique type for every lambda. Even identical-looking lambdas in different TUs produce different template instantiations. This is expected and safe — the ODR exemption covers this.

**Prevention:**
1. **Keep them in the header.** These are header-only by design — `inline template` functions must stay in `video_workflow_utils.h`.
2. **If refactoring for code sharing** (the goal is to reduce duplication across video/picture subsystems), extract a **common header** in a shared location (e.g., `core/job_state_utils.h`), keeping the `template<class Fn> inline` pattern intact.
3. **Use `using` declarations consistently.** If moving to `core::` namespace, ensure ALL call sites update their `using` declarations simultaneously. Mismatched `using` declarations in different TUs that point to old and new locations are an ODR violation.
4. **The `inline` keyword is critical.** `template<class Fn>` alone provides the ODR exemption, but `inline` is good practice for header-defined templates — it signals intent and prevents some compilers from issuing ODR-violation diagnostics.

**Detection:** ODR violations are **undefined behavior, not compilation errors**. Detection requires:
- Search for `withJobState(` and `withActionJobState(` across ALL source files (not just video subsystem) to ensure no stale references.
- The `using videoworkflow::withJobState;` declarations at `video_batch_execution.cpp:31` and `video_process.cpp:27` must both point to the same definition.
- After refactoring, run a clean rebuild (not incremental) to force all TUs to recompile with the new definition.

### Pitfall 4: Anonymous Namespace Fragmentation from File Splitting (OPTIM-02)

**What goes wrong:** Splitting `video_batch_execution.cpp` (804 lines) into multiple `.cpp` files creates **separate anonymous namespaces** in each new translation unit. Functions that currently call each other through anonymous namespace linkage become unavailable across file boundaries.

**Why it happens:** Per C++ standard §7.3.1.1 (since C++11):
> Unnamed namespaces as well as all namespaces declared directly or indirectly within an unnamed namespace have internal linkage, which means that any name that is declared within an unnamed namespace has internal linkage.

Each translation unit gets its **own unique anonymous namespace**. When a single `.cpp` file is split:
- `noteStopRequest` calls `withJobState` — if `noteStopRequest` moves to a new `.cpp` file, it must still have access to `withJobState` via the header (which is already the case — `withJobState` is in `video_workflow_utils.h`).
- `monitorEncodingProgress` calls `noteStopRequest`, `getEncodingProgress`, `reportEncodingStatus` — if these are split across files, the cross-references break unless the called functions are also in the header.
- `runEncodingTask` calls `createEncodingState`, `reportEncodingStatus`, `markRunningNoProgress` — same issue.

**Specific risks in this codebase:**
1. **`EncodingExecutionContext` and `EncodingProgressState`** are structs defined in the anonymous namespace (lines 87-329). They are LOCAL to `video_batch_execution.cpp`. Any function that takes `EncodingExecutionContext&` as a parameter CANNOT be moved to another `.cpp` file unless the struct definition is also moved to a shared header.
2. **`using` declarations** (lines 30-31): `using videoworkflow::withActionJobState;` and `using videoworkflow::withJobState;` are at file scope. Each new `.cpp` file would need these duplicated — not a problem as long as they all refer to the same header location.
3. **Include dependencies**: The current file includes `immer/atom.hpp`, `immer/vector.hpp` — any new `.cpp` file that uses `EncodingProgressState` (which depends on `immer::atom` and `immer::vector`) must also include these headers.
4. **Static initialization order fiasco (SIOF):** The `EncodingProgressState` constructor initializes `progressCtx` (a `progress::ProgressContext`). If split files introduce static-duration objects that depend on initialization order across translation units, the SIOF applies. Per cppreference: "Within a single translation unit, the fiasco does not apply because the objects are initialized from top to bottom."

**Prevention — "safe split" strategy given 0-header-modification constraint:**
1. **Do NOT move `EncodingExecutionContext` or `EncodingProgressState`** — they stay in the main `.cpp` file.
2. **Extract only leaf functions** that don't reference these structs: `truncateForProgressLabel`, `makeSlotLabel`, `getStateLabel` (these depend only on `std::string`, `fs::path`, `appctx::EncodingState`).
3. **`tryReadProgressData`** and **`getEncodingProgress`** could move to a separate file since they depend on `appctx::AppContext`, `appctx::EncodingState`, and `ProgressData` — all from headers.
4. **Structure as internal implementation files:** Create `video_batch_execution_progress.cpp` and `video_batch_execution_utils.cpp` — new files detected by `xmake.lua:49` (`add_files("src/**.cpp")`). No header modifications needed.
5. **Each new file gets its own anonymous namespace** — copy the relevant `using` declarations, includes, and namespace aliases.
6. **Re-verify `EncodingExecutionContext` member functions** (all 15 of them on lines 186-328) — these are tied to the struct and must stay.

**Detection of split failures:**
- **Linker errors** for unresolved symbols: functions that moved to a new file but are still called from the original file via anonymous-namespace names.
- **Duplicate symbol errors**: if a function is copy-pasted instead of moved, resulting in two definitions across translation units.
- **`using` declaration mismatch**: compilation error if a moved function uses `withJobState` but the new file lacks the `using videoworkflow::withJobState;` declaration.

### Pitfall 5: Scope Creep — Turning Tech Debt Into a Feature Sprint

**What goes wrong:** Tech debt milestones devolve into mini-feature development under the guise of "while we're touching this code anyway."

**Why it happens:** The temptation to "fix" things beyond the defined scope is strong when:
- A pattern looks "inelegant" and "could be better"
- New C++26 features seem applicable
- The milestone has momentum and feels like "cleanup time"

**Specific anti-patterns for this milestone:**

| Anti-Pattern | Example | Why It's Wrong |
|-------------|---------|----------------|
| **Over-abstracting templates** | Adding a concept-constrained, polymorphic `JobStateAccessor<T>` CRTP base class | `withJobState`/`withActionJobState` serve exactly 2 subsystems with identical patterns. The shared-helper refactoring should remain simple — extract to a common header, don't build a framework. |
| **Header restructuring** | Moving `PackPlan` from `pack_service.h` to a new `pack_types.h` | Violates the 0-header-modifications constraint. Changes the include graph for all consumers. |
| **"Better" tests** | Rewriting test assertions to use Catch2 matchers, `SECTION`, or `GENERATE` | These are style changes, not tech debt. Every rewritten line is a regression risk. |
| **Performance micro-optimizations** | Changing `std::function` callbacks in `PackPlan` to `std::move_only_function` or template callables | `std::function` is the current contract. Changing it introduces template instantiation complexity and potential ODR issues. |
| **C++26 feature adoption** | Using `std::execution`, `std::generator`, or `std::inplace_vector` | C++26 features are for feature milestones, not debt resolution. They introduce new failure modes and compiler edge cases on clang-cl. |
| **Changing public API** | Adding parameters to `videobatch::runEncodingTasks` | The function signature in `video_batch_execution.h` is the public contract. Cannot be modified. |
| **Adding logging/diagnostics** | Inserting `spdlog::debug` calls in refactored code "for better observability" | Behavioral change. If observability is needed, it belongs in a separate milestone. |
| **Renaming for clarity** | Renaming `markRunningNoProgress` to `markJobRunning` | The function name is established in v1.1 audit. Renaming creates inconsistency between the audit document and codebase. |
| **Inlining everything** | Making all extracted functions `constexpr` or `[[gnu::always_inline]]` | Unnecessary optimization. Functions are already in anonymous namespaces — the compiler inlines aggressively. |

**Prevention:**
1. **Work from the milestone checklist** in PROJECT.md lines 46-50. If an idea isn't on that list, it doesn't belong in v1.2.
2. **The "would this change any assertion?" test:** If a change could possibly alter test output, assertion count, or pass/fail status, it's a behavioral change → stop.
3. **The "would the v1.1 audit need updating?" test:** The v1.1 audit (v1.1-MILESTONE-AUDIT.md) documents 10 extracted functions across 4 files. If the refactoring would invalidate any of those entries, it's scope creep.
4. **Header modification check:** `git diff --stat HEAD -- '*.h' '*.hpp'` must return no output. Any change to any `.h` file violates the constraint.

---

## Moderate Pitfalls

### Pitfall 6: VERIFICATION.md Backfill Becoming a Rewrite (DEBT-03)

**What goes wrong:** Backfilling VERIFICATION.md turns into rewriting or reinterpreting historical decisions. The VERIFICATION.md should document **what was verified and how**, not re-litigate design choices.

**Prevention:**
- Source from existing artifacts: v1.0-MILESTONE-AUDIT.md, v1.1-MILESTONE-AUDIT.md, commit history.
- Use the VERIFICATION.md template (verify each requirement with evidence, not opinion).
- Do NOT add "improvement suggestions" or "future work" sections — those belong in PROJECT.md or roadmap files.

### Pitfall 7: Immer Persistent Data Structure Copy Semantics

**What goes wrong:** The codebase uses `immer::map` and `immer::vector` (persistent/immutable data structures). When splitting `video_batch_execution.cpp`, the `immer::atom<SharedSnapshot>` in `EncodingProgressState` uses lock-free updates. Any refactoring that changes how this atom is accessed could introduce subtle race conditions.

**Prevention:**
- Do not change any `immer::atom::update()` or `immer::atom::load()` call pattern.
- Do not introduce new shared state or atom wrappers around split code.
- The `EncodingExecutionContext` should remain a monolithic struct containing the atom — splitting atom access across files is unsafe.

### Pitfall 8: xmake `add_files("src/**.cpp")` Auto-Discovery

**What goes wrong:** The xmake build file (line 49) uses `add_files("src/**.cpp")` — a glob pattern that auto-discovers new `.cpp` files. Adding a new file like `video_batch_execution_utils.cpp` in `src/video/` will be automatically picked up on the next build. This is convenient but can mask issues:
- If the new file has compilation errors, the entire target fails.
- If the new file introduces a duplicate symbol, the linker error message won't clearly identify which file is the culprit.

**Prevention:**
- Run `xmake build` immediately after creating each new `.cpp` file to catch issues early.
- The test target (lines 75: `add_files("src/**.cpp|main.cpp")`) also uses glob — new files are automatically included in test compilation.

---

## Minor Pitfalls

### Pitfall 9: Clang-cl `-ftrivial-auto-var-init=pattern`

**What goes wrong:** The xmake.lua (line 9) sets `-ftrivial-auto-var-init=pattern`, which initializes all automatic variables with a pattern. When refactoring and adding explicit initializers, this flag can mask uninitialized-variable bugs that would otherwise be caught by static analysis.

**Prevention:** This flag is a safety net, not a substitute. Always explicitly initialize variables. The flag is already in place and should remain.

### Pitfall 10: `#pragma once` vs. Include Guards

**What goes wrong:** All headers use `#pragma once` (non-standard but universally supported). If a new internal header is created (unlikely given the 0-header-modification constraint), it must also use `#pragma once` for consistency. Mixing include guards and `#pragma once` is safe but inconsistent.

**Prevention:** If a new header must be created (only if unavoidable for the file split), use `#pragma once` to match the codebase convention.

### Pitfall 11: `constexpr` and `static` in Anonymous Namespaces

**What goes wrong:** Adding `static` or `constexpr` to functions in anonymous namespaces is redundant (anonymous namespace already provides internal linkage). However, it's not harmful. The pitfall is in copy-pasting functions: if a `static` function is moved between files, changing the `static` to anonymous-namespace scope is necessary but easy to miss.

**Prevention:** All extracted helper functions should be in anonymous namespaces (per D-01 from v1.1 audit). No `static` functions at file scope.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| DEBT-01 (explicit `.compact`) | Adding designator for wrong struct field | Verify `PackPlan` field order before editing; place `.compact = true` as last field |
| DEBT-02 (duplicate test) | Deleting test with unique coverage | Keep line 132 test; remove only redundant `compact` assertion (line 161) |
| DEBT-03 (VERIFICATION.md) | Scope creep into redesign | Source from existing audit artifacts only |
| OPTIM-01 (template refactoring) | ODR violation from header move | Keep templates in header; extract to shared `core/job_state_utils.h` if needed |
| OPTIM-02 (file splitting) | Anonymous namespace fragmentation | Extract only leaf functions; keep `EncodingExecutionContext` in original file |

## Items That Could Silently Change Behavior Even When Tests Pass

| Scenario | Mechanism | Detection Difficulty |
|----------|-----------|---------------------|
| Designated initializer reordering changing struct layout | C++20 padding rules with designated initializers in wrong order | **MEDIUM** — clang warns about designator order mismatch |
| Template instantiation divergence across TUs | Two TUs instantiate `withJobState` with different `Fn` types; linker picks one | **HIGH** — no diagnostic required; manifests as intermittent failures |
| `std::function` lambda capture lifetime change | Moving a lambda body to a named function changes capture semantics | **LOW** — compilation error if captures are wrong; runtime crash if captured by reference to moved-from local |
| `immer::atom` update timing change | Splitting code that performs `.update()` on the atom across files introduces different sequencing | **HIGH** — lock-free atom updates are correct as-is; any refactoring must preserve exact call order |

---

## Sources

- [cppreference: Aggregate Initialization (designated initializers, default member initializers)](https://en.cppreference.com/w/cpp/language/aggregate_initialization) — HIGH confidence
- [cppreference: One Definition Rule (ODR) — inline, template exemptions](https://en.cppreference.com/w/cpp/language/definition) — HIGH confidence
- [cppreference: Namespaces — unnamed namespaces, internal linkage since C++11](https://en.cppreference.com/w/cpp/language/namespace#Unnamed_namespaces) — HIGH confidence
- [cppreference: Static Initialization Order Fiasco (SIOF)](https://en.cppreference.com/w/cpp/language/siof) — HIGH confidence
- [cppreference: Templates — instantiation, specialization, ODR merging](https://en.cppreference.com/w/cpp/language/templates) — HIGH confidence
- `src/video/video_workflow_utils.h` — inline template definitions for `withJobState`/`withActionJobState` — HIGH confidence (primary source)
- `src/pack/pack_service.h` — `PackPlan` struct with `bool compact = true` default — HIGH confidence (primary source)
- `tests/pack_service_tests.cpp` — test cases at lines 98 and 132 — HIGH confidence (primary source)
- `src/video/video_batch_execution.cpp` — `EncodingExecutionContext`, `EncodingProgressState`, anonymous namespace functions — HIGH confidence (primary source)
- `xmake.lua` — build configuration, `add_files("src/**.cpp")` glob — HIGH confidence (primary source)
- `.planning/v1.1-MILESTONE-AUDIT.md` — architectural decisions D-01 through D-06 — HIGH confidence (project artifact)
- `.planning/PROJECT.md` — milestone scope and constraints — HIGH confidence (project artifact)

