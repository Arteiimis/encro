# Phase 3: Forensics - Research

**Researched:** 2026-05-23
**Domain:** C++ thread-local RAII error context stacking, inline LOG_ERROR message augmentation, lock-free environment snapshot
**Confidence:** HIGH

## Summary

Phase 3 adds forensic diagnostics to the logging layer: when an error occurs, LOG_ERROR automatically serializes a complete diagnostic chain -- which file, which pipeline stage, which retry attempt, and the specific failure -- plus a point-in-time environment snapshot of the encoding runtime. The mechanism is entirely RAII: developers place one `ScopedErrorContext ctx("stage", "detail")` at a function boundary, and everything else happens automatically.

The design leverages the existing Phase 1 custom LOG_* macro layer (D-01 injection points) and mirrors the Phase 2 ScopedTimer RAII pattern (move-only, noexcept destructor, `movedFrom_` flag). Thread safety comes from `thread_local` storage for the context stack and lock-free reads (immer atom, atomic variables) for the environment snapshot. No new dependencies are required -- this is pure C++26 on top of the Phase 1/2 foundation.

**Primary recommendation:** Add `ScopedErrorContext` to `src/logging/logging.h` alongside `ScopedTimer`, modify LOG_ERROR/LOG_CRITICAL macros to append TLS context chain, add `captureEnvironmentSnapshot()` as a free function reading existing `EncodingExecutionContext` state, and place context guards at all existing ScopedTimer sites plus retry loop boundaries.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Use thread-local RAII scoped stack -- `thread_local std::vector<ContextFrame>`, per-thread independent. No cross-thread sharing or propagation.
- **D-02:** Context depth limit 16 frames. Exceeding drops oldest frames and appends `[truncated]` marker.
- **D-03:** Context chain serialized inline in LOG_ERROR message body. Format: `"error message [context: file.mkv > encode stage > retry 2/3 > FFmpeg exit code 1]"`. Auto-appended by LOG_ERROR macro when TLS stack non-empty.
- **D-04:** Each ContextFrame renders as `"stage(detail)"`, frames joined by `" > "`. Example: `"input.mkv > encode(retry 2/3) > ffmpeg(exit 1)"`.
- **D-05:** Snapshot contains: active slot count + per-slot file paths, pending queue file count, current FFmpeg subprocess PID and command line. Triggered by LOG_ERROR/LOG_CRITICAL, emitted as separate log block immediately after error line.
- **D-06:** Snapshot via `logging::captureEnvironmentSnapshot()` free function. Reads `AppContext&` RuntimeContext and related state pointers. Fast, non-blocking -- no locks (reads atomics and immer structures).
- **D-07:** `ScopedErrorContext` is an RAII class -- ctor `ScopedErrorContext(std::string_view stage, std::string_view detail)` pushes frame onto TLS stack, destructor pops. `noexcept` destructor following Phase 2 D-11 precedent. Placed in `src/logging/logging.h`.
- **D-08:** Placement mirrors ScopedTimer (Phase 2 D-19/D-20): video.scan, probe, encode, pack; picture.scan, compress, pack; pack.execute. Additional placement at retry loop boundaries in encode_runner and WebP adaptive encoding.
- **D-09:** `eh::Result<T>` remains completely unchanged (`std::expected<T, std::string>`). Context lives only in TLS stack -- not carried or propagated through Result type.

### Claude's Discretion
- **ScopedErrorContext move-only, not copyable:** Mirroring ScopedTimer's move-only semantics. Move construction marks source `movedFrom_` = true to prevent double-pop.
- **Snapshot capture does not acquire locks:** `captureEnvironmentSnapshot()` reads only atomics and `immer::atom` structures. Safe to call on error path (which may hold locks).
- **Context frame format:** stage and detail are `std::string_view`, zero-copy. Frame content is formatted at log-record time (not at construction), so string_view must remain valid for the ScopedErrorContext lifetime.
- **Coexistence with ScopedTimer:** ScopedErrorContext and ScopedTimer are independent RAII types, can be nested/overlapping. Common pattern: `ScopedTimer timer("encode"); ScopedErrorContext ctx("encode", filename);` in same scope.
- **Thread safety:** TLS stack is thread-safe by definition, no mutex needed. immer structures read by `captureEnvironmentSnapshot()` are lock-free. Entire context/snapshot mechanism is signal-handler-safe (Phase 2 D-23 prohibits logging in signal handlers, but context accumulation happens before signal delivery).

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FOR-01 | On error, auto-output operation traceback (file -> stage -> retries -> specific error) | Implemented via TLS context stack + `LOG_ERROR` macro augmentation. ScopedErrorContext placed at pipeline stage boundaries and retry loops. Context chain serialized by D-04 format. |
| FOR-02 | On error, output environment snapshot (concurrent slots, processed/remaining files, FFmpeg process state) | `captureEnvironmentSnapshot()` reads `EncodingExecutionContext::activeStates()`, `pendingTotal()`, `finished()` via lock-free atomics + immer atom. FFmpeg PID/cmdline requires extending `EncodingState` to capture subprocess info from `exec2()`. |
| FOR-03 | Error context uses thread-local RAII scoped stack, serialized at call site (no spdlog MDC dependency) | `thread_local std::vector<ContextFrame>` with ScopedErrorContext RAII push/pop. Context chain resolved in macro expansion before `SPDLOG_LOGGER_CALL` queues to async worker -- avoids Pitfall #3. |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Context frame storage (TLS stack) | Utility (logging) | -- | Thread-local data structure with no external dependencies; lives in `logging` namespace alongside macros |
| Context frame push/pop (RAII guard) | Utility (logging) | -- | ScopedErrorContext is a pure RAII type, no business logic; placed at function boundaries in processing code |
| Context chain serialization (macro) | Utility (logging) | -- | LOG_ERROR/LOG_CRITICAL macro expansion resolves TLS stack into formatted string before SPDLOG_LOGGER_CALL |
| Environment snapshot capture | Utility (logging) | Processing (video) | Free function reads cross-cutting state from `EncodingExecutionContext` and `AppContext` but owns no state |
| FFmpeg subprocess state tracking | Processing (video) | Utility (logging) | `EncodingState` / `EncodingExecutionContext` owns the subprocess metadata; snapshot function reads it |
| Pipeline stage boundary context | Processing (video/picture/pack) | Utility (logging) | ScopedErrorContext placed at the same boundaries as ScopedTimer -- owned by processing layer, type from logging layer |
| Retry loop context | Processing (video) | Utility (logging) | encode_runner's WebP adaptive encoding loop and standard encoding failure path need per-attempt context |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C++26 `thread_local` | (language) | Per-thread context stack storage | Zero external dependency; standard C++ with guaranteed initialization |
| `std::vector` | (stdlib) | ContextFrame stack container | Max 16 frames -- trivial allocation, no custom data structure needed |
| `std::string_view` | (stdlib) | Zero-copy context frame data | stage and detail are pointers to caller-owned strings; no heap allocation on push/pop |
| `immer::atom` | existing (via Conan/xmake) | Lock-free environment snapshot reads | Already used by EncodingProgressState::snapshot for active slot tracking |
| `std::atomic` | (stdlib) | Lock-free counter reads | Already used by EncodingProgressState::counters for finished/pending |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `spdlog` (via SPDLOG_LOGGER_CALL) | existing | Async log dispatch | Context chain appended to message body before SPDLOG_LOGGER_CALL -- no spdlog API changes |
| `fmt::format` | existing | Context chain string formatting | Pre-format the `[context: ...]` suffix in macro expansion at call site (Pitfall #3 prevention) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `thread_local std::vector<ContextFrame>` | spdlog MDC (Mapped Diagnostic Context) | MDC is documented as not supported in async logging mode (PITFALLS.md Pitfall #3). Requires synchronous logging or custom log_msg extension. D-01 explicitly rules this out. |
| `std::string_view` for frame data | `std::string` (owned copy) | Owned copy is safer (no lifetime dependency) but heap-allocates on every push/pop. string_view chosen for zero-cost on non-error paths -- caller guarantees lifetime. |
| `std::array<ContextFrame, 16>` (fixed-size stack) | `std::vector<ContextFrame>` | Fixed array avoids heap allocation entirely but `std::vector` with max-16-reserve is simpler and the 16-element vector is small enough that the allocation cost is negligible. Both approaches valid; vector chosen for flexibility. |
| Inline snapshot in same log line | Separate snapshot log block | Inline makes the log line very long. Separate block (D-05 specifies "separate log block immediately after error line") keeps error line scannable while making snapshot detail available. |

**No new package dependencies:** This phase uses only C++ standard library features and existing project dependencies (spdlog, immer, fmt). No package installation required.

## Architecture Patterns

### System Architecture Diagram

```text
                    ┌──────────────────────────────────────────────┐
                    │           LOG_ERROR("msg")                    │
                    │         (macro expansion site)                │
                    └──────────────────┬───────────────────────────┘
                                       │
                         ┌─────────────┴─────────────┐
                         │ 1. Read TLS context stack  │
                         │    (thread_local vector)   │
                         └─────────────┬─────────────┘
                                       │
                         ┌─────────────┴─────────────┐
                         │ 2. Format context chain    │
                         │    "msg [context: a > b]" │
                         └─────────────┬─────────────┘
                                       │
                         ┌─────────────┴─────────────┐
                         │ 3. SPDLOG_LOGGER_CALL     │
                         │    (queues to async worker)│
                         └─────────────┬─────────────┘
                                       │
                         ┌─────────────┴─────────────┐
                         │ 4. IF level is err/crit:  │
                         │    captureEnvironmentSnap │
                         │    LOG_INFO(snapshot)      │
                         └───────────────────────────┘

  ScopedErrorContext Lifecycle:
  ┌──────────────────────────────────────────────────────────────────┐
  │  void processFile(path) {                                        │
  │    ScopedErrorContext ctx("encode", path.filename());  // PUSH   │
  │    ScopedTimer timer("encode");                                   │
  │    // ... work ...                                                │
  │    if (error) {                                                   │
  │      LOG_ERROR("fail");  // reads TLS stack, formats chain       │
  │    }                                                              │
  │  }  // ctx destructor runs → POP; timer destructor logs elapsed   │
  └──────────────────────────────────────────────────────────────────┘

  Environment Snapshot Data Flow:
  ┌──────────────────────┐     ┌─────────────────────────┐
  │ EncodingExecutionContext │───▶│ activeStates()           │
  │   .snapshot (immer::atom)│   │   → per-slot file paths  │
  │   .counters (atomic)    │───▶│ finished.load()          │
  │                         │───▶│ pendingTotal             │
  └──────────────────────┘     └──────────┬──────────────┘
                                          │
  ┌──────────────────────┐               │
  │ EncodingState         │               │
  │   .subprocessPid      │──────────────▶│
  │   .subprocessCmdline  │               │
  └──────────────────────┘               │
                                          ▼
                                captureEnvironmentSnapshot()
                                  → formatted snapshot block
                                  → LOG_INFO(...)
```

### Recommended Project Structure (Phase 3 additions)

```
src/logging/
├── logging.h          # +ScopedErrorContext class, +captureEnvironmentSnapshot() declaration
│                      #   +TLS context stack (thread_local), +ContextFrame struct
│                      #   Modified: LOG_ERROR/LOG_CRITICAL macros append context chain
│                      #   Modified: LOG_ERROR/LOG_CRITICAL trigger snapshot
├── log_tags.h         # +INFRA_FORENSICS tag (if needed for snapshot logging)
│
tests/
├── logging_error_context_test.cpp   # NEW: ScopedErrorContext lifecycle, push/pop, move, nesting
├── logging_snapshot_test.cpp        # NEW: captureEnvironmentSnapshot() format and content
```

### Pattern 1: Thread-Local RAII Context Stack

**What:** A per-thread `std::vector<ContextFrame>` with RAII push/pop via ScopedErrorContext. The LOG_ERROR macro reads the stack at macro expansion time (on the calling thread) and formats the chain into the message body before SPDLOG_LOGGER_CALL queues the message to the async worker.

**Why this pattern:** Resolves PITFALLS.md Pitfall #3 (TLS with async logging) -- context is resolved at the call site, on the calling thread, before the message is queued. The async worker thread never touches TLS. This is the same pattern as Phase 1's source location injection (D-03): bake everything into the message body at macro expansion time.

**Key design points:**
- `ContextFrame` stores `std::string_view stage` and `std::string_view detail` -- zero-copy, caller guarantees lifetime
- Max 16 frames; exceeding drops oldest (FIFO eviction) with `[truncated]` marker
- Push on ScopedErrorContext construction, pop on destruction
- Move-only (copy deleted), `movedFrom_` flag prevents double-pop
- `noexcept` destructor (Phase 2 D-11 precedent)

### Pattern 2: Macro-Level Context Chain Injection

**What:** LOG_ERROR and LOG_CRITICAL macros call a helper function `logging::formatContextChain()` that reads the TLS stack on the calling thread and returns a formatted string suffix. The suffix is concatenated into the message body before `SPDLOG_LOGGER_CALL`.

**Why this pattern:** The existing LOG_ERROR macro format string is `"[{}:{}] {}"`. Phase 3 extends this to `"[{}:{}] {}{}"` where the fourth argument is the context chain suffix (empty string when TLS stack is empty). The format happens in the macro expansion, on the calling thread -- same principle as the existing `fmt::format(__VA_ARGS__)` call (Phase 1 D-01 design).

**Key design points:**
- context chain is a `std::string` (owned, safe to pass to async queue)
- Empty string when TLS stack is empty -- zero overhead on non-error paths
- Format: `" [context: stage1(detail1) > stage2(detail2)]"` per D-03/D-04
- No branching in macro -- the helper function returns empty string or formatted chain

### Pattern 3: Lock-Free Environment Snapshot

**What:** `logging::captureEnvironmentSnapshot(appctx::AppContext&)` is a free function that reads concurrent state without locks and formats a diagnostic block. It is called immediately after LOG_ERROR/LOG_CRITICAL formats its message, emitting a separate LOG_INFO line with the snapshot.

**Why this pattern:** The snapshot captures "what else was happening" at failure time. Lock-free reads are critical because error paths may be called while holding mutexes (e.g., inside `EncodingState::mtx` lock scope in `failEncoding()`). Using immer atom and atomic reads ensures the snapshot is always safe to call.

**Key design points:**
- Reads `EncodingExecutionContext::activeStates()` (immer::atom -- lock-free)
- Reads `EncodingExecutionContext::counters()` (std::atomic -- lock-free)
- FFmpeg subprocess info: requires extending `EncodingState` with `std::optional<int> subprocessPid` and `std::optional<std::string> subprocessCmdline`
- Snapshot output format: structured as multi-line or single-line INFO block with key=value pairs
- Function signature needs `AppContext&` or at minimum the `EncodingExecutionContext` -- the latter requires making the execution context pointer available to the logging layer

### Anti-Patterns to Avoid

- **Capturing string_view pointing to temporaries:** ContextFrame stores string_view to caller-owned strings. Do NOT pass `std::format("retry {}", n)` directly as detail -- the temporary is destroyed at the semicolon. Store the formatted string in a local variable first.
- **Calling LOG_ERROR from ScopedErrorContext destructor:** The destructor must be noexcept and must not log (it pops the frame). Logging from the destructor would trigger recursive context chain formatting.
- **Modifying eh::Result<T>:** D-09 explicitly prohibits this. Context propagates through TLS, not through the Result type. Do not add context fields to `std::expected`.
- **Making captureEnvironmentSnapshot take locks:** The function may be called inside a mutex scope (e.g., inside `failEncoding` which holds `state.mtx`). All reads must be lock-free.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Per-thread context storage | Custom thread-local map/registry | `thread_local std::vector<ContextFrame>` | Standard C++ guarantees correct initialization per thread. No external synchronization needed. |
| Lock-free concurrent state reads | Mutex-guarded snapshot | immer::atom + std::atomic (existing in EncodingExecutionContext) | Already present in codebase. Lock-free reads prevent deadlock on error paths. |
| RAII scope guard | Custom scope-exit mechanism | ScopedErrorContext class (copying ScopedTimer pattern) | Proven pattern from Phase 2. Same move-only semantics, noexcept destructor, movedFrom_ flag. |
| Context chain string building | Manual string concatenation in macro | `fmt::format` (already in codebase) | Consistent with Phase 1 message body formatting. Resolved at call site before async queue. |

**Key insight:** The entire mechanism is built on patterns already established in Phase 1 and Phase 2. The only genuinely new code is the TLS stack data structure and the `captureEnvironmentSnapshot()` function. The macro modification is an extension of the existing `"[{}:{}] {}"` format string pattern. The ScopedErrorContext is a near-copy of ScopedTimer with a push/pop instead of timing.

## Runtime State Inventory

> This phase adds new code and modifies existing macros; it is not a rename/refactor/migration phase. No runtime state inventory needed.

## Common Pitfalls

### Pitfall 1: string_view Lifetime Mismatch

**What goes wrong:** Passing a temporary `std::string` (e.g., from `std::format()`) as the `detail` argument to ScopedErrorContext. The temporary is destroyed at the end of the full expression, leaving the ContextFrame with a dangling string_view.

**Why it happens:** `ScopedErrorContext(std::string_view stage, std::string_view detail)` stores raw string_view -- no copy. If detail is `std::format("retry {}/{}", attempt, max)`, the returned std::string is a temporary destroyed at the semicolon.

**How to avoid:**
```cpp
// WRONG: dangling string_view
ScopedErrorContext ctx("encode", std::format("retry {}/{}", attempt, max));

// RIGHT: store formatted string in local variable first
auto detail = std::format("retry {}/{}", attempt, max);
ScopedErrorContext ctx("encode", detail);
```

**Warning signs:** Garbled context in error output; ASAN use-after-free in `formatContextChain()`.

### Pitfall 2: Recursive LOG_ERROR from Context Destructor

**What goes wrong:** Adding LOG_ERROR or LOG_WARN inside ScopedErrorContext's destructor. Since the frame is still on the TLS stack during destruction, any LOG_ERROR call would read the stack (including the frame being popped) and produce incorrect context.

**Why it happens:** Natural instinct to log "context ended" -- but the frame is popped AFTER the destructor body, and any logging inside the destructor would see the frame still on the stack.

**How to avoid:** The destructor must ONLY pop the frame (no logging). The `movedFrom_` guard pattern from ScopedTimer ensures the destructor body is trivial:
```cpp
~ScopedErrorContext() noexcept {
    if (movedFrom_) { return; }
    popContextFrame();  // only action -- no logging
}
```

**Warning signs:** Unexpected context frames in error output; duplicate or stale context entries.

### Pitfall 3: captureEnvironmentSnapshot() Called Without Execution Context

**What goes wrong:** `captureEnvironmentSnapshot()` is called when no encoding is active (e.g., during picture processing or pack-only mode). The `EncodingExecutionContext` pointer is null or not set, and the snapshot function crashes or produces meaningless output.

**Why it happens:** The LOG_ERROR/LOG_CRITICAL macro unconditionally calls `captureEnvironmentSnapshot()`, but video encoding state only exists during video encoding. Picture and pack pipelines have different state structures.

**How to avoid:** `captureEnvironmentSnapshot()` must accept an optional/nullable reference to encoding state. When encoding state is unavailable (picture/pack pipeline), it produces a minimal snapshot: just the current pipeline stage and error context, no slot/FFmpeg info. Alternatively, separate snapshot functions per pipeline type, or pass a `std::optional<EncodingExecutionContext const*>`.

**Warning signs:** Null pointer dereference in snapshot function; snapshot output shows zero slots when encoding was active.

### Pitfall 4: Context Frame Overflow Masking Root Cause

**What goes wrong:** Deeply nested call stacks (recursive directory scanning, nested pack grouping) exceed 16 frames. The FIFO eviction drops the oldest (outermost) frames -- which typically contain the most important context (which file, which pipeline). The truncated chain shows only the innermost frames, which may not identify the file.

**Why it happens:** D-02 specifies dropping oldest frames. For a shallow pipeline (scan -> probe -> encode -> pack = 4 frames + retry loops), 16 frames is ample. But deeply nested operations (recursive pack grouping, directory traversal) can accumulate frames from helper functions.

**How to avoid:** Place ScopedErrorContext only at meaningful boundaries (pipeline stages, retry loops) -- NOT in every helper function. Follow the D-08 placement guide: mirror ScopedTimer sites only. If 16-frame overflow occurs, the `[truncated]` marker alerts the developer to check the outer context separately. The truncation marker should indicate how many frames were dropped: `[truncated: 3 frames dropped]`.

**Warning signs:** Error output shows `[truncated]` but the root file path is missing; context seems to start mid-operation.

## Code Examples

### ScopedErrorContext Class Definition

```cpp
// Source: Patterned after src/logging/logging.h ScopedTimer (Phase 2)
// File: src/logging/logging.h (addition, alongside ScopedTimer)

namespace logging {

struct ContextFrame {
    std::string_view stage;
    std::string_view detail;
};

namespace detail {

// TLS context stack -- internal to logging implementation
auto pushContextFrame(std::string_view stage, std::string_view detail) -> void;
auto popContextFrame() -> void;
auto formatContextChain() -> std::string;

}  // namespace detail

class ScopedErrorContext {
    bool movedFrom_{false};

public:
    ScopedErrorContext(std::string_view stage, std::string_view detail) {
        detail::pushContextFrame(stage, detail);
    }

    ~ScopedErrorContext() noexcept {
        if (movedFrom_) { return; }
        detail::popContextFrame();
    }

    ScopedErrorContext(ScopedErrorContext const&) = delete;
    auto operator=(ScopedErrorContext const&) -> ScopedErrorContext& = delete;

    ScopedErrorContext(ScopedErrorContext&& other) noexcept
        : movedFrom_(false) {
        other.movedFrom_ = true;
    }

    auto operator=(ScopedErrorContext&& other) noexcept -> ScopedErrorContext& {
        if (this != &other) {
            movedFrom_ = false;
            other.movedFrom_ = true;
        }
        return *this;
    }
};

}  // namespace logging
```

### Modified LOG_ERROR Macro

```cpp
// Source: Extends existing LOG_ERROR in src/logging/logging.h
// Context chain resolved on calling thread (Pitfall #3 prevention)

#define LOG_ERROR(...)                                                    \
  do {                                                                     \
    auto const __encro_ctx_chain = logging::detail::formatContextChain();    \
    SPDLOG_LOGGER_CALL(                                                     \
      loggerPtr(),                                                          \
      spdlog::level::err,                                                   \
      "[{}:{}] {}{}",                                                       \
      logging::detail::shortFile(__FILE__),                                 \
      __LINE__,                                                             \
      fmt::format(__VA_ARGS__),                                             \
      __encro_ctx_chain                                                     \
    );                                                                      \
    /* Snapshot captured here -- note: this is on the calling thread */     \
  } while(0)
```

### Context Frame Rendering

```cpp
// Source: D-04 specification -- each frame as "stage(detail)"

// Render single frame:
//   detail non-empty: "stage(detail)"
//   detail empty:     "stage"

// Render chain (3 frames):
// " [context: encode(input.mkv) > retry(2/3) > ffmpeg(exit 1)]"

// Render chain (empty):
// ""  (empty string -- no context suffix appended)

// Render chain with truncation:
// " [context: (truncated 3) > encode(input.mkv) > ... > ffmpeg(exit 1)]"
```

### captureEnvironmentSnapshot() Usage

```cpp
// Source: D-05/D-06 specification
// Called immediately after LOG_ERROR/LOG_CRITICAL message is formatted
// Output written as separate LOG_INFO line(s)

// Example snapshot output (single INFO line):
// "[video_process.cpp:441] Environment: active-slots=3/8 pending=12 finished=45 subprocess=[pid=28476 cmd='ffmpeg -i input.mkv ...']"

// Example snapshot output when no encoding active:
// "[picture_process.cpp:597] Environment: pipeline=picture.compress (no encoding slots)"
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual error context threading (passing context strings through function parameters) | TLS RAII scoped stack with macro injection | Phase 3 (this phase) | Zero manual context propagation; context accumulates automatically at boundaries |
| spdlog MDC (Mapped Diagnostic Context) for error metadata | Custom TLS stack resolved at call site | Phase 3 (design decision D-01) | Works with async logging; MDC is documented as incompatible with async mode |
| Environment state inferred from scattered log lines | Single structured snapshot block at error time | Phase 3 (this phase) | Answers "what else was happening?" without grep-and-correlate |
| eh::Result<T> carrying context strings in error payload | Context in TLS only, Result unchanged | Phase 3 (design decision D-09) | Zero coupling between error propagation and context accumulation |

**Deprecated/outdated:**
- **spdlog MDC for async contexts:** spdlog's MDC feature explicitly does not support async logging (Pitfall #3). The custom TLS stack with call-site resolution is the correct approach.
- **Manual context string threading:** The existing codebase manually constructs error messages with file paths and details at each LOG_ERROR call site. Phase 3 makes this automatic -- developers add ScopedErrorContext at boundaries, and all downstream LOG_ERROR calls benefit.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `EncodingState` can be extended with `subprocessPid` and `subprocessCmdline` fields without breaking existing serialization or job state | Code Examples / Architecture Patterns | LOW -- EncodingState is not serialized to JSON (it's runtime-only); jobstate::Store uses separate TaskRecord types |
| A2 | `captureEnvironmentSnapshot()` can receive the necessary state (EncodingExecutionContext or equivalent) without restructuring the AppContext data flow | Architecture Patterns | MEDIUM -- the function needs access to encoding-specific state that currently lives in local variables within `runEncodingTask()`. May require making the execution context accessible at a broader scope (e.g., storing a pointer in RuntimeContext). |
| A3 | `exec2()` can be extended to capture the subprocess PID without breaking the existing API | Common Pitfalls / Architecture Patterns | LOW -- `bp::child::id()` returns the PID; the question is where to store it (EncodingState or a separate structure) |
| A4 | The `do { ... } while(0)` wrapper on LOG_ERROR (Phase 3 addition) is compatible with all existing LOG_ERROR call sites | Code Examples | LOW -- do-while(0) is the standard C macro safety wrapper; existing call sites are all complete statements ending with `;` |
| A5 | `ContextFrame` with `std::string_view` fields does not need heap allocation and is safe with the max-16-frames limit | Architecture Patterns | LOW -- string_view is trivially copyable and trivially destructible; vector of 16 frames is ~32 bytes per frame on 64-bit (two pointers) |

## Open Questions

1. **How does `captureEnvironmentSnapshot()` access `EncodingExecutionContext`?**
   - What we know: The execution context (`EncodingExecutionContext`) is a local variable in `videobatch::runEncodingTasks()`. It is not stored in `AppContext` or `RuntimeContext`. The snapshot function needs a reference to it.
   - What's unclear: Whether to (a) store a pointer in RuntimeContext during encoding, (b) pass the execution context through the encode call chain to where LOG_ERROR fires, or (c) use a thread-local pointer set by the encoding task.
   - Recommendation: Option (a) -- store a `std::atomic<EncodingExecutionContext*>` in RuntimeContext, set at encoding start, cleared at encoding end. The snapshot function reads this atomically. This is safe because the pointer is only read (not dereferenced for mutation) and the pointed-to object outlives the encoding task.

2. **Should context frames show detail when detail is empty?**
   - What we know: D-04 specifies `stage(detail)` format but does not address the empty-detail case.
   - What's unclear: Whether to render `"encode()"` or just `"encode"` when detail is empty.
   - Recommendation: Render as `"stage"` when detail is empty (no parentheses). This is cleaner: `"scan > probe > encode"` vs `"scan() > probe() > encode()"`. Only show `stage(detail)` when detail is non-empty.

3. **Is the snapshot a single line or multi-line block?**
   - What we know: D-05 says "separate log block immediately after error line" but does not specify single vs multi-line.
   - What's unclear: Whether to emit one INFO line with all snapshot data or multiple lines.
   - Recommendation: Single INFO line using key=value format (`"Environment: active-slots=3/8 pending=12 ..."`). Multi-line snapshots break grep-ability and complicate Phase 4 JSON output.

4. **Should LOG_WARN also trigger the environment snapshot?**
   - What we know: D-05 says "triggered by LOG_ERROR/LOG_CRITICAL" only.
   - What's unclear: Whether warnings during encoding (e.g., `"ffmpeg exited with non-zero code"` at `video_encode_runner.cpp:292`) should also capture snapshot context.
   - Recommendation: Follow the spec exactly -- snapshot only on LOG_ERROR and LOG_CRITICAL. The LOG_WARN at line 292 is followed by a return value check that may trigger LOG_ERROR if the overall encode fails. Adding snapshot to LOG_WARN would produce redundant output on retry-able warnings.

## Environment Availability

Step 2.6: SKIPPED (Phase 3 is purely internal C++ code modifications -- no external tool, service, CLI, runtime, or database dependencies beyond what Phase 1 and Phase 2 already established. The existing xmake + clang-cl + Catch2 test infrastructure is used as-is.)

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3 (`catch2/catch_all.hpp`, custom runner in `tests/test_main.cpp`) |
| Config file | none -- Catch2 configured via `CATCH_CONFIG_RUNNER` in test_main.cpp |
| Quick run command | `xmake build tests && xmake run tests "[logging]"` |
| Full suite command | `xmake build tests && xmake run tests` |

### Phase Requirements -> Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| FOR-03 | ScopedErrorContext pushes frame on construction | unit | `xmake run tests "ScopedErrorContext pushes frame on construction"` | No -- Wave 0 |
| FOR-03 | ScopedErrorContext pops frame on destruction | unit | `xmake run tests "ScopedErrorContext pops frame on destruction"` | No -- Wave 0 |
| FOR-03 | ScopedErrorContext is move-only (not copyable) | unit (static_assert) | `xmake run tests "ScopedErrorContext is not copyable"` | No -- Wave 0 |
| FOR-03 | ScopedErrorContext destructor is noexcept | unit (static_assert) | `xmake run tests "ScopedErrorContext destructor is noexcept"` | No -- Wave 0 |
| FOR-03 | Moved-from ScopedErrorContext does not double-pop | unit | `xmake run tests "ScopedErrorContext move transfers ownership"` | No -- Wave 0 |
| FOR-03 | Nested ScopedErrorContext produces ordered chain | unit | `xmake run tests "Nested ScopedErrorContext produces ordered chain"` | No -- Wave 0 |
| FOR-03 | Self-move-assignment is safe | unit | `xmake run tests "ScopedErrorContext self-move-assignment is safe"` | No -- Wave 0 |
| FOR-03 | Empty stage name edge case does not crash | unit | `xmake run tests "ScopedErrorContext with empty stage name"` | No -- Wave 0 |
| FOR-01 | LOG_ERROR appends context chain when TLS stack non-empty | unit | `xmake run tests "LOG_ERROR appends context chain"` | No -- Wave 0 |
| FOR-01 | LOG_ERROR produces no context suffix when TLS stack empty | unit | `xmake run tests "LOG_ERROR without context"` | No -- Wave 0 |
| FOR-01 | Context chain format matches " [context: stage(detail) > ...]" | unit | `xmake run tests "Context chain format is correct"` | No -- Wave 0 |
| FOR-01 | Context depth limit 16 frames with truncation | unit | `xmake run tests "Context frame overflow triggers truncation"` | No -- Wave 0 |
| FOR-02 | captureEnvironmentSnapshot() produces snapshot when encoding active | integration | `xmake run tests "Environment snapshot during encoding"` | No -- Wave 0 |
| FOR-02 | captureEnvironmentSnapshot() is safe when encoding not active | integration | `xmake run tests "Environment snapshot without encoding"` | No -- Wave 0 |
| FOR-02 | Snapshot contains slot count, pending count, FFmpeg info | integration | `xmake run tests "Snapshot contains required fields"` | No -- Wave 0 |
| FOR-01 | ScopedErrorContext at pipeline boundaries captures full chain on error | integration | `xmake build e2e_tests && xmake run e2e_tests "[forensics]"` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake run tests "[logging]"` -- all logging unit tests
- **Per wave merge:** `xmake run tests` -- full unit + integration suite
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/logging_error_context_test.cpp` -- covers FOR-01, FOR-03: ScopedErrorContext lifecycle, context chain formatting, frame overflow truncation, LOG_ERROR integration
- [ ] `tests/logging_snapshot_test.cpp` -- covers FOR-02: captureEnvironmentSnapshot format, content verification, null-encoding-state safety
- [ ] Test framework extension: `logging::detail::resetContextStack()` or `ScopedContextReset` RAII fixture to clear TLS stack between test cases (prevents cross-test contamination)
- [ ] Test helper: `registerCapturingLoggerForContext()` -- ostream_sink logger (adapting existing `registerCapturingLoggerForTimer` pattern from `logging_scoped_timer_test.cpp`)

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A (local CLI tool, no auth) |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A |
| V5 Input Validation | yes (indirect) | Context frame `std::string_view` values come from developer-authored string literals and file paths -- no user-controlled content in context stages. File paths in detail should use `displaytext::pathToUtf8String()` for consistency with existing practice. |
| V6 Cryptography | no | N/A |

### Known Threat Patterns for C++ RAII Logging

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Log injection via manipulated file paths in context detail | Spoofing | File paths rendered via `displaytext::pathToUtf8String()` which controls encoding; newlines in paths are harmless (rendered as literal `\n` in log line, not actual newline) |
| Information disclosure via environment snapshot file paths | Information Disclosure | Snapshot only logged at INFO level to file sink (not stdout). Console output already shows file paths in progress bars -- no new exposure. |
| Denial of service via excessive context frame push | Denial of Service | Max 16 frames (D-02). TLS stack size is bounded by design. No dynamic allocation per frame beyond the initial 16-element vector. |
| Thread safety violation in context stack access | Tampering | `thread_local` storage is thread-safe by definition -- each thread has its own stack. No shared mutable state. |

## Sources

### Primary (HIGH confidence)
- [VERIFIED: codebase] `src/logging/logging.h` -- existing LOG_ERROR macro injection point (D-01), format string `"[{}:{}] {}"`, ScopedTimer class pattern with move-only semantics and noexcept destructor
- [VERIFIED: codebase] `src/logging/log_tags.h` -- module tag constants; Phase 3 may add `infra.forensics` tag
- [VERIFIED: codebase] `src/video/video_encode_runner.cpp` -- `failEncoding()` LOG_ERROR call site (line 56), WebP retry loop LOG_ERROR call sites (lines 218, 227, 274), `runStandardEncoding` LOG_WARN (line 292)
- [VERIFIED: codebase] `src/video/video_batch_execution.h:134-275` -- `EncodingExecutionContext` with `setActive()`, `clearActive()`, `activeStates()`, `pendingTotal()`, `finished()`, `loadShared()` -- all the state needed by `captureEnvironmentSnapshot()`
- [VERIFIED: codebase] `src/video/video_process.cpp` -- ScopedTimer placement sites (lines 167, 188, 302, 396) and LOG_ERROR call sites (lines 267, 291, 441, 523, 534)
- [VERIFIED: codebase] `src/picture/picture_process.cpp` -- ScopedTimer placement sites (lines 290, 334, 413, 474) and LOG_ERROR call site (line 597)
- [VERIFIED: codebase] `src/pack/pack_service.cpp` -- ScopedTimer placement site (line 509)
- [VERIFIED: codebase] `src/core/error_handle.h` -- `eh::Result<T>` definition (will NOT be modified per D-09)
- [VERIFIED: codebase] `src/core/app_context.h` -- `EncodingState` struct, `RuntimeContext` with `immer::atom` pattern
- [VERIFIED: codebase] `tests/logging_scoped_timer_test.cpp` -- existing test patterns for RAII lifecycle, move semantics, noexcept, nesting, self-move safety (9 tests)
- [VERIFIED: project docs] `.planning/phases/03-forensics/03-CONTEXT.md` -- all D-01 through D-09 locked decisions and Claude's Discretion items
- [VERIFIED: project docs] `.planning/REQUIREMENTS.md` -- FOR-01, FOR-02, FOR-03 detailed requirements
- [VERIFIED: project docs] `.planning/research/PITFALLS.md` -- Pitfall #3 (TLS with async), Pitfall #8 (module tag chaos), Pitfall #9 (unbounded context growth)

### Secondary (MEDIUM confidence)
- [CITED: spdlog docs] spdlog MDC and async incompatibility -- confirmed via PITFALLS.md research citing DeepWiki and spdlog source. MDC uses `thread_local` which async worker cannot access.
- [VERIFIED: codebase] `src/utils/utils.cpp:48-49` -- `bp::child` construction in `exec2Impl()`; `bp::child::id()` returns PID -- confirmed via Boost.Process documentation

### Tertiary (LOW confidence)
- None -- all claims verified against existing codebase or project documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies; all infrastructure (spdlog, immer, std::atomic, thread_local, fmt) already present and exercised in Phase 1/2
- Architecture: HIGH -- the RAII pattern and macro injection are established in the codebase; ScopedErrorContext is a direct copy of ScopedTimer's design; TLS stack is a straightforward data structure; the only design question (A2: how to expose EncodingExecutionContext to the snapshot function) has a clear recommendation (store atomic pointer in RuntimeContext)
- Pitfalls: HIGH -- five pitfalls identified based on PITFALLS.md research and analysis of the existing code; string_view lifetime is the most subtle and important

**Research date:** 2026-05-23
**Valid until:** 2026-06-23 (30 days -- stable domain, no fast-moving dependencies)
