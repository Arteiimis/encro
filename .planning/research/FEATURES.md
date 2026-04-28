# Feature Landscape: v1.2 Tech Debt & Code Quality

**Domain:** C++ CLI tool tech debt resolution milestone
**Researched:** 2026-04-28
**Overall confidence:** HIGH (source code verified directly; industry patterns supplemented by authoritative sources)

## Executive Perspective

This is a **cleanup milestone**, not a feature milestone. The primary deliverable is reduced technical debt through five concrete items. Each item is independently verifiable and none depend on each other for completion — they share no code paths and can be parallelized. The milestone delivers one user-visible fix (implicit `.compact` default), one test-quality improvement (duplicate removal), one process compliance artifact (VERIFICATION.md backfill), and two structural optimizations (template refactoring + file splitting).

**Key insight from Martin Fowler's "Is High Quality Software Worth the Cost?":** Internal quality directly reduces the cost of future feature delivery. Even elite teams accumulate cruft, but they continuously remove it. This milestone embodies that principle — every item either eliminates a correctness risk or reduces the friction of future changes.

---

## Table Stakes

These are "must fix" items. Missing any of these means the milestone is incomplete. They address correctness bugs, dead code, or process gaps.

| Feature | Why Expected | Complexity | Category | Notes |
|---------|--------------|------------|----------|-------|
| **DEBT-01: Explicit `.compact` in picture compress-paint path** | The PackPlan at `picture_process.cpp:474-482` relies on struct default (`compact = true`). All other PackPlan builders explicitly set `.compact`. When struct defaults change (e.g., if `compact` default changes to `false`), this site silently breaks. One-line fix: add `.compact = true` to the designated initializer. | **Low** (1 line added) | DEBT — Correctness | Regression prevention. The pattern `compact = !ctx.config.fullProgress` is used elsewhere; here it's always `true` because pictures always use compact mode. Explicit is defensive. |
| **DEBT-02: Remove duplicate test case** | `pack_service_tests.cpp:98-130` tests "selectPackPlanIndexes preserves compact from source plan" covering both compact=true and compact=false. Lines 132-162 test "selectPackPlanIndexes delegates to named helpers..." which ALSO asserts `result.compact == true` at line 161 — a redundant check already covered by the first test. | **Low** (remove compact assertion from second test, or merge tests) | DEBT — Dead Code | The second test's purpose is verifying the v1.1 lambda refactor (named helper delegation). The compact assertion at line 161 is vestigial. Remove that single CHECK line, not the entire test case. |
| **DEBT-03: Backfill VERIFICATION.md** | Phase 01 (Compact Progress Mode) and Phase 02 (Compact Mode Gap Fixes) shipped without formal VERIFICATION.md documents. The existing v1.0-MILESTONE-AUDIT.md confirms tests pass, but doesn't follow the structured verification format. For process compliance and future auditability, structured verification is expected. | **Low-Medium** (documentation, ~2 pages each) | DEBT — Process | Tests prove behavior; verification docs prove that the *right* behavior was delivered and how it maps to requirements. These are complementary artifacts. |

---

## Differentiators

These are "nice to have" items. They improve maintainability but don't fix bugs. A milestone without them still addresses all correctness issues.

| Feature | Value Proposition | Complexity | Category | Notes |
|---------|-------------------|------------|----------|-------|
| **OPTIM-01: Refactor shared template helpers** | `withJobState` and `withActionJobState` in `video_workflow_utils.h` are already clean templates (5-7 lines each). The issue: they live in namespace `videoworkflow` under `src/video/`, but `picture_process.cpp` also uses them. The duplication concern is that picture subsystem currently accesses them via a video-namespaced header. Moving them to `core/job_state_utils.h` (or `core/app_context_utils.h`) reduces coupling and clarifies that these are general-purpose helpers, not video-specific. | **Low-Medium** (move header, update includes) | OPTIM — Structural | **Not CRTP or type erasure needed.** The current template pattern is already optimal — it accepts a callable and forwards it. CRTP would add complexity for zero gain; type erasure (std::function) would add overhead; C++20 concepts could constrain the template parameter but aren't necessary for a 5-line function. The simplest refactor is relocation to `core/`. |
| **OPTIM-02: Split video_batch_execution.cpp** | File is 700 lines with clear logical boundaries: (1) EncodingProgressState struct + helpers (~200 lines), (2) monitorEncodingProgress + startEncodingMonitor (~115 lines), (3) runEncodingTask (~110 lines), (4) runEncodingWithoutProgress + runEncodingTasks (~170 lines), (5) helper functions (~105 lines). Each section has a single responsibility. Splitting enables faster incremental compilation and clearer code navigation. | **Medium** (create 3-4 new .cpp files, extract struct to internal header) | OPTIM — Structural | High risk of introducing build system changes if headers change. Recommended approach: keep existing `.h` public API; all new files are internal `.cpp` files in same directory. |

---

## Anti-Features

Explicit decisions about what NOT to do in this milestone.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Full rewrite of any subsystem** | The goal is incremental cleanup, not greenfield. Even for the file split, the behavior must remain byte-identical. | Targeted surgical changes, verified by existing 910-assertion test suite. |
| **Changing PackPlan struct defaults** | The struct default `compact = true` in `pack_service.h:48` is correct. Removing it would break the picture path more severely. | DEBT-01 fixes the call site, not the default. |
| **CRTP or heavy template metaprogramming** | The `withJobState` helpers are 5-line templates. CRTP would turn a simple forwarding function into a class hierarchy. | Keep the existing template pattern; relocate to shared location. |
| **std::function replacement for templates** | Type erasure via `std::function` adds a heap allocation and virtual dispatch for every call. The template approach is zero-cost. | Keep the template; it's already the right pattern. |
| **Splitting into more than 4 new files** | Over-splitting creates navigation fatigue. The 700-line file has 4 natural sections; more granularity would scatter related code. | 3-4 new `.cpp` files maximum. |
| **Changing public API or headers** | Both v1.0 and v1.1 maintained zero header modifications. This milestone should continue that discipline. | All changes internal to `.cpp` files; any new headers are private/internal. |
| **Adding new tests for refactored code** | The existing 910 assertions (215 test cases) already cover the refactored paths. Adding tests for the refactoring itself tests the refactoring, not the behavior. | Rely on existing test suite as regression safety net. |

---

## Feature Dependencies

```
DEBT-01 ← independent
DEBT-02 ← independent
DEBT-03 ← independent
OPTIM-01 ← independent (but benefits from being done before OPTIM-02 so video_batch_execution.cpp uses relocated helpers)
OPTIM-02 ← independent (but uses withJobState/withActionJobState which are targeted by OPTIM-01)
```

**Dependency insight:** No hard dependencies exist. All five items can be done in parallel. However, doing OPTIM-01 before OPTIM-02 is *tactically better* — the split files can `#include` the relocated header from its new location, avoiding a second pass later.

---

## MVP Recommendation

**Prioritize DEBT over OPTIM.** The debt items fix correctness/process issues; the optimizations improve structure. Order of execution:

### Phase 1: Must-Fix Debt (parallelizable)
1. **DEBT-01** — 1-line fix, 5 minutes of work, immediate test verification
2. **DEBT-02** — 1-line removal, 5 minutes of work, verify test suite still passes
3. **DEBT-03** — Write 2 VERIFICATION.md files from existing audit + test data (~1 hour each)

### Phase 2: Structural OPTIM (OPTIM-01 first, then OPTIM-02)
4. **OPTIM-01** — Relocate template helpers to `core/` (~30 min), update includes (~10 min)
5. **OPTIM-02** — Split `video_batch_execution.cpp` (~2 hours, highest risk)

**Defer nothing.** All five items are independently small enough to complete in a single milestone. The quality gate says to distinguish "must fix" from "nice to have" — DEBT items are must-fix, OPTIM items are nice-to-have but both belong in this milestone because they're small.

---

## Item-by-Item Deep Dive

### DEBT-01: Explicit `.compact` in picture_process.cpp

**Current state (lines 474-482):**
```cpp
auto const plan = pack::PackPlan{
    .groups = groupedPics,
    .outputDir = outputDir,
    .zipNameForIndex = [picturePackNamingState](...) { ... },
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .removeOnFailure = true
    // ⚠ .compact missing — relies on struct default `compact = true`
};
```

**Fix:** Add `.compact = true` after `.removeOnFailure = true`.

**Why this matters:** Every other PackPlan construction site explicitly sets compact:
- `video_process.cpp:434` → `.compact = !ctx.config.fullProgress`
- `packer.cpp:820` → `.compact = true`
- `pack_service_tests.cpp:112,124` → `.compact = false` / `.compact = true`

The picture path is the ONLY site that doesn't set it. This is a latent regression risk — if anyone changes the struct default (e.g., because a future feature needs `compact = false` by default), the picture path silently breaks.

**Verification:** Run existing picture tests. No behavioral change expected — `.compact = true` is already the effective value via the default.

---

### DEBT-02: Remove duplicate test assertion

**Current state:**
- Test A (line 98): "selectPackPlanIndexes preserves compact from source plan" — tests compact=false and compact=true preservation. **Valuable.** Covers behavior.
- Test B (line 132): "selectPackPlanIndexes delegates to named helpers instead of lambda-wrapping-lambda" — tests zipNameForIndex and progressLabelForIndex remapping. **Valuable.** Covers v1.1 refactor behavior. BUT line 161 `CHECK(result.compact == true)` is redundant with Test A.

**Fix:** Remove line 161 (`CHECK(result.compact == true);`) from Test B. The compact preservation behavior is exhaustively tested by Test A (both true and false). Test B should only test the named helper delegation.

**Verification:** Test suite should still pass with all assertions intact (removing 1 redundant check; the remaining checks at lines 157, 159 still test the delegation behavior).

---

### DEBT-03: Backfill VERIFICATION.md

**What valuable vs. redundant looks like:**

| Valuable Content | Redundant Content |
|------------------|-------------------|
| Which requirements from the roadmap were validated, with evidence | Repeating test code or test assertions verbatim |
| How each decision mapped to implemented behavior | Listing line counts or file names without context |
| Coverage gaps acknowledged (what was NOT verified) | Copying the existing milestone audit report |
| Environment/configuration used for verification | Describing how to run tests (that's in README/build docs) |
| Cross-subsystem interaction validation (e.g., compact propagation) | Individual test case summaries already visible in test files |

**Template for each phase:**

```markdown
# Phase 0X Verification: [Phase Name]

**Date verified:** [date]
**Verifier:** [name]

## Requirements Coverage

| Requirement ID | Requirement | Validated? | Evidence |
|---------------|-------------|------------|----------|
| REQ-01 | Compact single bar default | ✓ | E2E test `compact_progress_default` passes |
| ... | ... | ... | ... |

## Decision Validation

| Decision | Expected Behavior | Observed Behavior | Status |
|----------|------------------|-------------------|--------|
| D-0X | ... | ... | ✓ |

## Cross-Subsystem Checks

- Picture compress path uses compact → verified via `picture_compress_tests`
- Video encode path uses compact → verified via `video_batch_execution_tests`
- Pack-only path uses compact → verified via `packer_tests`

## Coverage Gaps

- [Any scenarios not covered by tests]
- [Any edge cases deferred]

## Environment

- OS: [platform]
- Compiler: clang-cl (C++26)
- FFmpeg: [version]
```

**Phase 01 (Compact Progress Mode):** Cover the 4 E2E flows (default encoding+pack, full-progress, pack-only, picture), cross-subsystem compact propagation, `--verbose-echo` precedence.

**Phase 02 (Compact Mode Gap Fixes):** Cover the `.compact` field propagation fix in `selectPackPlanIndexes`, pack-only path verification, job state integration.

---

### OPTIM-01: Refactor shared template helpers

**Current state:** `video_workflow_utils.h` in namespace `videoworkflow` under `src/video/`.

```cpp
template<class Fn>
inline auto withJobState(appctx::AppContext& ctx, Fn&& fn) -> bool {
  if (auto* store = maybeJobState(ctx); store != nullptr) {
    std::forward<Fn>(fn)(*store);
    return true;
  }
  return false;
}

template<class Fn>
inline auto withActionJobState(
  appctx::AppContext& ctx,
  std::optional<std::string> const& actionId,
  Fn&& fn
) -> bool {
  if (!actionId.has_value()) { return false; }
  return withJobState(ctx, [&](jobstate::Store& store) { fn(store, actionId.value()); });
}
```

**Why these are already the right pattern:**
- Templates with `Fn&&` → zero-cost abstraction, no `std::function` overhead
- Perfect forwarding → no unnecessary copies
- Boolean return → callers can check if job state existed
- Simple predicate → no CRTP hierarchy needed
- No type erasure → no virtual dispatch

**The refactoring is relocation, not redesign:**
1. Move `withJobState` + `withActionJobState` + `maybeJobState` to new `src/core/job_state_utils.h` in namespace `jobstate` (or keep in dedicated namespace)
2. Update `#include` in `video_workflow_utils.h` to forward to new location (or remove entirely if nothing else uses it)
3. Update `#include` in `video_batch_execution.cpp` and `video_process.cpp` to point to new location
4. Add `#include` in `picture_process.cpp` to new location

**What NOT to do:**
- CRTP: `template<class Derived> struct WithJobState { ... }` — turns a 5-line helper into a class hierarchy. Overkill.
- Type erasure: `std::function<void(jobstate::Store&)>` — heap allocates. Slower. The template is zero-cost.
- C++20 concepts: `template<std::invocable<jobstate::Store&> Fn>` — adds constraint checking but not needed for a 5-line function. Would be a separate improvement, not a bug fix.

---

### OPTIM-02: Split video_batch_execution.cpp

**Why 700 lines is worth splitting (evidence):**

Google's internal research (from their code review tooling, cited in numerous engineering blogs) found that code review effectiveness drops significantly after ~400 lines per file. LLVM's codebase (one of the largest C++ projects) doesn't specify a hard limit but organizes by logical component — most LLVM `.cpp` files are 200-500 lines. Microsoft's STL implementation averages ~400 lines per file. While no single "threshold" is universally agreed upon, 700 lines is past the point where navigation friction increases and incremental compilation benefits from splitting.

**Natural boundaries in the file:**

| Section | Lines | Logical Unit | Proposed File |
|---------|-------|-------------|---------------|
| Helper functions (noteStopRequest, markRunningNoProgress, finalizeEncodeResult, truncateForProgressLabel, makeSlotLabel, getStateLabel) | ~105 | Pure helpers, no state | Keep in `video_batch_execution.cpp` (or extract to `video_batch_helpers.cpp`) |
| `EncodingProgressState` struct + methods | ~200 | Progress state management | `video_encoding_progress_state.cpp` |
| `tryReadProgressData`, `getEncodingProgress`, `reportEncodingStatus`, `createEncodingState` | ~75 | Progress I/O | Move into progress section or merge |
| `monitorEncodingProgress`, `startEncodingMonitor` | ~115 | Async monitor thread | `video_encoding_monitor.cpp` |
| `runEncodingTask` | ~110 | Single encoding task execution | `video_encoding_task.cpp` |
| `runEncodingWithoutProgress`, `runEncodingTasks` (public) | ~170 | Entry points | Keep in `video_batch_execution.cpp` |

**Recommended split (conservative — 3 new files):**

1. **`video_encoding_progress_state.cpp`** — EncodingProgressState struct + makeInitialSnapshot, createOverallBar, makeSlotBars, plus the progress I/O helpers. ~275 lines.
2. **`video_encoding_monitor.cpp`** — monitorEncodingProgress + startEncodingMonitor. ~115 lines.
3. **`video_encoding_task.cpp`** — runEncodingTask. ~110 lines.
4. **`video_batch_execution.cpp`** — Remaining ~200 lines: helper functions + runEncodingWithoutProgress + runEncodingTasks (public API).

**Why an internal header may be needed:** The `EncodingProgressState` struct is used across monitor and task code. Instead of polluting the public header `video_batch_execution.h`, create `src/video/video_encoding_progress_state_internal.h` (`.h` suffix for internal use, not `.inc` to avoid confusion with LLVM's convention).

**Risk mitigation:**
- All 4 `.cpp` files compile into the same translation unit set — no library boundary changes
- The public header `video/video_batch_execution.h` is NOT modified
- Existing tests link against the same object files — no test changes needed
- Build system (xmake) only needs new source file entries, no new dependencies

**Verification:** After splitting, run full test suite (910 assertions, 215 test cases). Binary smoke test with all 4 E2E flows. All outputs must be identical.

---

## Competing Solutions Table

### OPTIM-01: Template Helper Refactoring

| Solution | Pros | Cons | Verdict |
|----------|------|------|---------|
| **Relocate to `core/` (recommended)** | Zero behavioral change, minimal diff, correct namespace | Requires updating ~10 include lines | ✓ SIMPLE — do this |
| CRTP pattern | Compile-time dispatch, no templates in headers | 5-line helper becomes 20+ line class hierarchy, opaque error messages | ✗ Overengineered |
| Type erasure (`std::function`) | Single type, easier to read | Heap allocation per call, blocks inlining | ✗ Performance regression |
| C++20 concepts | Better error messages, self-documenting | Does nothing about the "shared location" problem; this is about *where* not *how* | ✗ Misdirected — solves wrong problem |

### OPTIM-02: File Splitting Thresholds

| Splitting Strategy | Pros | Cons | Verdict |
|-------------------|------|------|---------|
| **3 new files (conservative)** | Low risk, clear boundaries, 200 lines each | 4 total files for one module | ✓ RECOMMENDED |
| No splitting | Zero risk | 700-line file remains; compile-time unchanged | ✗ Defers the problem |
| 6+ micro-files | Maximum granularity | 100-line files with 2-3 functions each; navigation fatigue | ✗ Over-split |
| Single file with `#pragma region` | Zero build system changes | Compiler still compiles 700 lines; regions are editor-specific and don't reduce compilation units | ✗ Cosmetic only |

---

## Confidence Assessment

| Area | Confidence | Reason |
|------|-----------|--------|
| DEBT-01 location and fix | **HIGH** | Source code verified directly at `picture_process.cpp:474-482`, compared against 3 other PackPlan sites |
| DEBT-02 duplicity | **HIGH** | Source code verified; both tests read and compared line-by-line |
| DEBT-03 template structure | **MEDIUM** | Pattern derived from project's existing documentation style; no VERIFICATION.md precedent exists in this repo to compare against |
| OPTIM-01 patterns | **HIGH** | Source code verified; template helpers read directly; C++ patterns (CRTP, type erasure, concepts) assessed against codebase conventions |
| OPTIM-02 boundaries | **HIGH** | Source code verified; function boundaries identified via AST-level reading of the file |
| File size conventions | **MEDIUM** | Industry consensus (Google, LLVM, Microsoft) converged on 300-500 line range but no formal specification exists |

---

## Sources

### Source Code (Primary — HIGH confidence)
- `src/picture/picture_process.cpp` — Lines 474-482: PackPlan construction without `.compact`
- `src/pack/pack_service.h` — Line 48: `bool compact = true;` struct default
- `tests/pack_service_tests.cpp` — Lines 98-162: Two test cases with redundant compact check
- `src/video/video_workflow_utils.h` — Lines 30-49: `withJobState` and `withActionJobState` templates
- `src/video/video_batch_execution.cpp` — Full 700-line file, function boundaries identified
- `src/video/video_process.cpp` — Line 434: `.compact = !ctx.config.fullProgress` pattern
- `src/pack/packer.cpp` — Line 820: `.compact = true` explicit setting

### Industry References (MEDIUM confidence)
- Martin Fowler, "Is High Quality Software Worth the Cost?" (2019, updated 2024) — https://martinfowler.com/articles/is-quality-worth-cost.html — Establishes the economic argument for continuous tech debt reduction
- LLVM Coding Standards — https://llvm.org/docs/CodingStandards.html — No explicit line-count limit but emphasizes modular organization and self-contained units
- Google C++ Style Guide — Evidenced indirectly via LLVM's documentation practices (sourced from same engineering culture); no explicit file-size threshold found

### C++ Pattern Analysis (HIGH confidence — training data corroborated by source code review)
- CRTP: Useful for static polymorphism, not for simple forwarding templates
- Type erasure (`std::function`): Runtime overhead; inappropriate for zero-cost abstractions
- C++20 concepts: Constrain templates but don't solve the "shared location" problem
- Free function templates in headers: The existing pattern is already optimal for `withJobState`/`withActionJobState`
