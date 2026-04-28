# Technology Stack — Tech Debt Resolution

**Project:** encro — C++26 CLI tool for video encoding + zip packing
**Researched:** 2026-04-28
**Clang version:** 22.1.4 (based on LLVM 22, targets `x86_64-pc-windows-msvc`)
**Confidence:** HIGH (verified via cppreference.com compiler support table, clang.llvm.org/cxx_status.html, and direct codebase inspection)

## Executive Summary

For a C++26 codebase doing tech debt cleanup (fixing implicit struct defaults, removing duplicate tests, refactoring shared template functions, splitting large .cpp files), the most impactful C++26 features are **not yet available** in clang-cl 22.1.4 at this date. Reflection (P2996R13), expansion statements (P1306R5), and concept template parameters (P2841R7) — the features that could substantially reduce template boilerplate — are all unimplemented. However, several smaller C++26 features ARE available and practically useful: pack indexing for cleaner template metaprogramming, `= delete("reason")` for self-documenting deletions, and the `std::function_ref` library type for non-owning callable wrapping. For the specific `withActionJobState`/`withJobState` duplication, the solution is NOT language features but **higher-level helper functions that encapsulate the repeated lock-extract-mark patterns** at call sites. Code splitting across compilation units requires zero xmake build system changes (the `add_files("src/**.cpp")` glob already handles new files automatically).

## Recommended Stack (No Changes)

The existing tech stack requires no additions for this milestone. All tech debt resolution uses already-provisioned tooling.

### Core Framework (Unchanged)

| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| C++26 (clang-cl) | LLVM 22.1.4 | Language | Already set via `set_languages("c++26")` in xmake.lua |
| xmake | 3.0+ | Build system | `add_files("src/**.cpp")` wildcard handles file splitting automatically |
| boost | any | CLI parsing (`program_options`), JSON | Already provisioned via `add_requires("boost[all]")` |
| immer | any | Persistent data structures (`map`, `vector`, `atom`) | Used for ActionIdMap, EncodeResultsMap |
| spdlog | any | Structured logging | Already used throughout |
| Catch2 | any | Test framework | 910 assertions across 215 test cases |

### C++26 Features Available for This Milestone

| Feature | Paper | Clang Since | Usefulness for This Milestone |
|---------|-------|-------------|-------------------------------|
| Pack indexing (`T...[N]`) | P2662R3 | Clang 19 | **MEDIUM**: Could simplify pack access in template metaprogramming, but `withJobState`/`withActionJobState` have no packs |
| `= delete("reason")` | P2573R2 | Clang 19 | **LOW**: Documentation improvement; not a refactoring tool |
| Placeholder variables (`auto _ = ...`) | P2169R4 | Clang 18 | **LOW**: Minor cleanup for intentionally-unused return values |
| Structured bindings can introduce a pack | P1061R10 | Clang 21 | **LOW**: No pack-based structured bindings in current code |
| `std::function_ref` (library) | P0792R14 | libc++ 16 | **MEDIUM**: Could replace `template<class Fn>` with type-erased wrapper if template instantiation count matters, but current usage is already minimal |
| Structured binding as condition | P0963R3 | Clang 21 | **LOW**: Useful in if-let patterns, not applicable here |
| Variadic friends | P2893R3 | Clang 20 | **N/A**: No template friend declarations exist |
| `constexpr` placement new | P2747R2 | Clang 20 | **N/A**: Not applicable to this refactoring |

### C++26 Features NOT Available (Cannot Use)

| Feature | Paper | Status | Why We'd Want It |
|---------|-------|--------|-----------------|
| Reflection | P2996R13 | NOT implemented | Could generate job state wrapper functions from Store method signatures, eliminating hand-written forwarding |
| Expansion statements | P1306R5 | NOT implemented | Could unroll template `for_each` over parameter packs |
| Concept template parameters | P2841R7 | NOT implemented | Would allow `template<concept C> requires ...` parameter constraints |
| Contracts | P2900R14 | NOT implemented | Could replace some runtime null checks with contract preconditions |
| `constexpr` exceptions | P3068R6 | NOT implemented | Would simplify error handling in constexpr contexts |

### C++20 Concepts Already Available (Apply Now)

The codebase already uses C++26 mode, so all C++20 features are available. **Concepts are the most underutilized feature** for the template refactoring task:

```cpp
// Current (unconstrained):
template<class Fn>
inline auto withJobState(appctx::AppContext& ctx, Fn&& fn) -> bool {
  if (auto* store = maybeJobState(ctx); store != nullptr) {
    std::forward<Fn>(fn)(*store);
    return true;
  }
  return false;
}

// Improved with C++20 concept constraint:
template<std::invocable<jobstate::Store&> Fn>
inline auto withJobState(appctx::AppContext& ctx, Fn&& fn) -> bool {
  // ... same body, but now with compile-time signature checking
}
```

This catches misuse at compile time (e.g., passing a callable with wrong parameter count) rather than at template instantiation.

## Recommended Approach for Each Tech Debt Item

### OPTIM-01: `withActionJobState`/`withJobState` Template Deduplication

**Do NOT change the templates themselves.** The templates are already minimal (5 lines each) and correctly factored. The duplication is in the **call site patterns** in `video_batch_execution.cpp`.

**Pattern to extract:** The repeated sequence of "lock mutex → extract optional fields → call `withActionJobState` with lambda → call specific Store method" appears 5+ times in `video_batch_execution.cpp`. 

**Recommended approach:** Create higher-level named free functions in the anonymous namespace (consistent with the D-01 decision from v1.1):

```cpp
// Example: Extract the "mark failed with extracted values" pattern
namespace {
auto markFinalStateWithLock(
  appctx::AppContext& ctx,
  std::mutex& mtx,
  std::optional<std::string>& actionId,
  bool success,
  std::string const& defaultFailureReason,
  std::optional<std::string> const& lastError,
  std::optional<std::string> const& lastStatus
) -> void {
  auto id = std::optional<std::string>{};
  auto reason = defaultFailureReason;
  auto status = std::optional<std::string>{};
  {
    auto lock = std::scoped_lock{mtx};
    id = actionId;
    if (lastError.has_value()) reason = lastError.value();
    else if (lastStatus.has_value()) { reason = lastStatus.value(); status = lastStatus; }
  }
  withActionJobState(ctx, id, [&](jobstate::Store& store, std::string const& aid) {
    if (success) {
      if (status) store.markSucceeded(aid, *status);
      else store.markSucceeded(aid);
    } else {
      store.markFailed(aid, reason);
    }
  });
}
}  // namespace
```

This eliminates 5+ copies of the identical lock-extract-store pattern without changing the existing template signatures or introducing new language features. No C++26 features needed — just good factoring consistent with the v1.1 lambda refactoring pattern.

### OPTIM-02: Split `video_batch_execution.cpp` (804 lines) into Multiple Compilation Units

**Build system impact: ZERO.** The xmake target uses `add_files("src/**.cpp")` which automatically picks up any `.cpp` file anywhere under `src/`. No xmake.lua changes required.

**Recommended split:**

| New File | Lines From Original | Content |
|----------|---------------------|---------|
| `src/video/video_batch_execution.cpp` (keep) | ~lines 687-804 | Public entry point `runEncodingTasks()` only |
| `src/video/video_batch_helpers.cpp` | ~lines 33-85 | `noteStopRequest`, `markRunningNoProgress`, `finalizeEncodeResult`, utility functions |
| `src/video/video_batch_progress.cpp` | ~lines 87-329 | `EncodingProgressState` struct (all progress bar management) |
| `src/video/video_batch_execution_context.cpp` | ~lines 184-329 (refactored) | `EncodingExecutionContext` struct (extracted from progress state, used by encoding logic) |
| `src/video/video_batch_encoding.cpp` | ~lines 331-685 | `EncodingExecutionContext` member functions + `runEncodingTask` + encoding loop + monitor |

**Anonymous namespace consideration:** The original file uses an anonymous namespace for file-local functions. When splitting, functions that need to be shared across new `.cpp` files must move to a named `detail` namespace in a shared header, or be declared `inline` in an internal header. Recommendation:

```cpp
// New file: src/video/video_batch_internal.h
#pragma once
// ... includes ...
namespace videobatch::detail {
  // Move shared structs and function declarations here
  struct EncodingProgressState { ... };
  struct EncodingExecutionContext { ... };
  void noteStopRequest(appctx::AppContext& ctx);
  // etc.
}
```

**Header impact:** The public `video_batch_execution.h` already exposes only `runEncodingTasks()`. No public header changes needed — the internal implementation split is entirely hidden.

### DEBT-01/02/03: No Stack Changes

These items (fix implicit `.compact` default, remove duplicate test, backfill VERIFICATION.md) are pure code/documentation changes requiring zero tooling additions.

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Template refactoring | Extract higher-level free functions in anonymous namespace | Use C++26 reflection to auto-generate wrappers | Reflection not implemented in clang-cl 22.1.4 |
| Template refactoring | Keep `template<class Fn>` with `std::invocable` constraint | Replace with `std::function_ref` to reduce template instantiations | Premature optimization; template count is already low (2 instantiations); `function_ref` adds indirection |
| Code splitting | Split into 3-4 .cpp files with `detail` namespace header | Use C++20 modules to avoid header dependencies | Modules on Windows/clang-cl are incomplete; simpler approach works |
| Code splitting | Keep anonymous namespace for truly private functions; `detail` namespace for shared internals | Move everything to `detail` namespace | Hybrid approach preserves compilation firewall where appropriate |
| Build system | Zero changes (wildcard globs cover new files) | Add explicit `add_files()` entries for each new file | Unnecessary; glob works, explicit listing adds maintenance burden |
| External tools | None needed | cpp-dependencies or include-what-you-use for include analysis | Nice-to-have, not needed for this simple split |

## Installation (No New Dependencies)

No new packages to install. The existing xmake.lua dependencies cover everything needed:

```bash
# Already provisioned via xmake.lua:
# add_requires("boost[all]")
# add_requires("thread-pool")
# add_requires("spdlog[fmt_external]")
# add_requires("fmt")
# add_requires("indicators")
# add_requires("immer")
# add_requires("libzippp")
# add_requires("catch2")
```

## Architecture Note: Template Pattern in Codebase

The `video_workflow_utils.h` template pattern is well-designed for its purpose:

```
┌─────────────────────────────────────────┐
│           video_workflow_utils.h        │
│                                         │
│  maybeJobState(ctx) → Store*  (nullable)│
│  withJobState(ctx, fn) → bool           │
│  withActionJobState(ctx, id, fn) → bool │
│  lookupPlannedOutputFile(map, path)     │
└──────────┬──────────────────────────────┘
           │ used by
    ┌──────┴──────┐
    │             │
 video_process  video_batch_execution
 (video encode  (batch encoding,
  orchestration) progress monitoring)
```

The picture subsystem (`picture_process.cpp`) does NOT use these templates — it has its own separate workflow that doesn't touch `jobstate::Store` directly. No cross-subsystem template sharing is needed.

## Sources

| Source | Confidence | URL/Reference |
|--------|-----------|--------------|
| cppreference C++26 compiler support | HIGH | https://en.cppreference.com/w/cpp/compiler_support/26 |
| Clang C++ status page (official) | HIGH | https://clang.llvm.org/cxx_status.html |
| Direct codebase inspection | HIGH | `src/video/video_workflow_utils.h`, `src/video/video_batch_execution.cpp` |
| xmake target documentation | MEDIUM | https://xmake.io/#/manual/project_target (xmake 3.0.8; glob patterns stable since 2.x) |
| clang-cl version verification | HIGH | `clang-cl --version` → 22.1.4 (local) |
| cppreference std::function_ref | HIGH | https://en.cppreference.com/w/cpp/utility/functional/function_ref |
