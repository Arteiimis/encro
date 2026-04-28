# Research Synthesis: v1.2 Tech Debt & Code Quality

**Synthesized:** 2026-04-28
**Confidence:** HIGH (reconciled from 4 researchers, primary source-verified)

## Executive Summary

All 5 items are independently shippable with no stack changes needed. The key tensions between researchers have been resolved below.

## Resolved Tensions

### 1. OPTIM-01: withActionJobState/withJobState Refactoring

**Tension resolved:**

Architecture and Pitfalls researchers both concluded the template helpers must stay in `video_workflow_utils.h` — moving them risks ODR violations. Picture subsystem does not use these helpers at all (it delegates job state via `runPackPlan` internally). There is no cross-subsystem sharing to justify relocation.

**Recommendation:** Keep templates in `video_workflow_utils.h`. The real refactoring is **call-site pattern deduplication** — the "lock mutex → extract optional → call withActionJobState → mark state" pattern repeats 5+ times in `video_batch_execution.cpp`. Extract higher-level free functions in the anonymous namespace (following v1.1 D-01 pattern).

### 2. OPTIM-02: Split video_batch_execution.cpp

**Tension resolved:**

Multiple researchers proposed 3-4 files, but Architecture identified a critical C++ constraint: anonymous namespace functions CANNOT call each other across translation units. The `EncodingExecutionContext` and `EncodingProgressState` structs anchor most functions. Without header changes they can't be shared.

**Recommendation:** Split into **2 files** only — `video_encoding_state.cpp` (NEW, ~420 lines) + `video_batch_execution.cpp` (MODIFIED, ~410 lines). Requires a **narrow D-01 exception**: move `EncodingProgressState` + `EncodingExecutionContext` from anonymous namespace to `videobatch::detail` namespace in `video_batch_execution.h` (+30 lines). This is not a lambda extraction — it's existing structs moving to enable the split. Zero behavioral change.

### 3. DEBT-02: Duplicate Test Case

**Tension resolved:**

Pitfalls researcher found the two tests are NOT duplicates — they validate different concerns:
- Line 98: property test (compact=true + compact=false propagation)
- Line 132: integration test (factory function remapping from v1.1)

**Recommendation:** Keep both tests. Remove ONLY the redundant `CHECK(result.compact == true)` on line 161. Do NOT delete the line 132 test (it provides unique coverage of `zipNameForIndex`/`progressLabelForIndex` remapping).

## Uncontested Items

### DEBT-01: Explicit `.compact` default
**Fix:** Add `.compact = true` to the PackPlan designated initializer at `picture_process.cpp:474-482`, after `.removeOnFailure = true` (last in declaration order). One line. Zero risk — currently identical behavior through struct default. Add `static_assert(std::is_aggregate_v<pack::PackPlan>)` for safety.

### DEBT-03: Backfill VERIFICATION.md
**Approach:** Create structured verification documents for Phase 01 and Phase 02 sourced from v1.0-MILESTONE-AUDIT.md and commit history. Template in FEATURES.md. ~1 hour each.

## Recommended Phase Structure

### Phase 1: Must-Fix Debt (parallelizable, all independent)

| # | Item | Effort | Risk |
|---|------|--------|------|
| 1 | DEBT-01: Fix implicit .compact default | 1 line, 5 min | Zero |
| 2 | DEBT-02: Remove redundant test assertion | 1 line, 5 min | Zero |
| 3 | DEBT-03: Backfill VERIFICATION.md x2 | ~2 hours | Zero |

### Phase 2: Structural Optimization

| # | Item | Effort | Risk |
|---|------|--------|------|
| 4 | OPTIM-01: Call-site pattern deduplication | ~30 min | Low (ODR-safe, no header changes) |
| 5 | OPTIM-02: Split video_batch_execution.cpp | ~2 hours | Medium (narrow D-01 exception, thorough testing needed) |

**OPTIM-02 should precede OPTIM-01** or be done independently — call-site dedup in a full file is harder than in a split file.

## D-01 Constraint Analysis

| Item | Header Change | Exposed Symbols | Justification |
|------|:---:|:---:|---|
| DEBT-01 | No | None | Pure .cpp change |
| DEBT-02 | No | None | Test file change |
| DEBT-03 | No | None | Documentation only |
| OPTIM-01 | No | None | Pattern dedup in anonymous namespace |
| OPTIM-02 | +30 lines | `videobatch::detail::*` only | Struct relocation, not lambda exposure. Minimal, well-defined exception. |

## Key Construction Constraints (from PITFALLS.md)

- **Immutable:** Do not change `immer::atom::update()`/`load()` call patterns (race condition risk)
- **Verification gate:** All 910 assertions must pass identically after every change
- **Header check:** `git diff --stat HEAD -- '*.h' '*.hpp'` should show only the OPTIM-02 exception
- **No scope creep:** If an idea isn't on the milestone checklist, it doesn't belong in v1.2
- **ODR safety:** `withJobState`/`withActionJobState` templates must stay header-only
- **Test coverage:** Removing line 161 assertion reduces assertions by exactly 1
