# Phase 3: Forensics - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-23
**Phase:** 3-forensics
**Areas discussed:** Error Context Threading Model, Context Chain Serialization, Environment Snapshot Scope, ScopedErrorContext Placement, Integration with eh::Result<T>
**Mode:** --auto (all areas auto-selected, recommended options chosen)

---

## Error Context Threading Model

| Option | Description | Selected |
|--------|-------------|----------|
| Thread-local TLS stack | `thread_local std::vector<ContextFrame>` per thread, matches FOR-03 spec | ✓ |
| Explicit ErrorContext& parameter | Thread context through every function signature — invasive | |
| spdlog MDC | Mapped Diagnostic Context built into spdlog — FOR-03 explicitly rejects | |

**[auto] Selected:** Thread-local TLS stack (recommended default)
**Rationale:** FOR-03 explicitly requires "thread-local RAII 作用域栈，在调用点序列化（不依赖 spdlog MDC）". TLS is simplest, requires zero API changes to existing functions.

---

## Context Chain Serialization Format

| Option | Description | Selected |
|--------|-------------|----------|
| Inline in LOG_ERROR message | Bracketed suffix appended to error message body | ✓ |
| Separate LOG_ERROR lines | Each context level as its own log line | |
| JSON-only (Phase 4) | Context only in structured output, human-readable unchanged | |

**[auto] Selected:** Inline in LOG_ERROR message body (recommended default)
**Rationale:** Keeps all diagnostic info on one line for grep/analysis. Format: `"error [context: file.mkv > encode > retry 2/3 > FFmpeg exit 1]"`. Human-readable first, Phase 4 adds JSON equivalent.

---

## Environment Snapshot Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal: active slots + remaining + FFmpeg PID/cmdline | Matching FOR-02 spec exactly | ✓ |
| Detailed: per-slot path/elapsed + all queued + process tree | Could bloat logs for large batches | |
| Configurable by verbosity | Depth controlled by --verbose level | |

**[auto] Selected:** Minimal snapshot (recommended default)
**Rationale:** Matches FOR-02 requirements precisely. Active slots, remaining queue size, current FFmpeg subprocess state. Detailed per-file info would bloat logs; users needing more can enable verbose mode.

---

## ScopedErrorContext Placement

| Option | Description | Selected |
|--------|-------------|----------|
| Pipeline stage boundaries + retry loops | Mirror ScopedTimer placement (Phase 2 D-19/D-20) | ✓ |
| Error-prone boundaries only | Only at FFmpeg subprocess call sites | |
| Every function entry | Full coverage but invasive | |

**[auto] Selected:** Pipeline stage boundaries + retry loops (recommended default)
**Rationale:** Mirrors ScopedTimer placement pattern established in Phase 2. Stage boundaries already represent meaningful context transitions. Retry loops add attempt-N detail.

---

## Integration with eh::Result<T>

| Option | Description | Selected |
|--------|-------------|----------|
| Context in TLS stack only | LOG_ERROR reads TLS; eh::Result stays plain std::string | ✓ |
| eh::Result carries context | Functions append context when propagating errors | |
| New eh::ResultWithContext<T> | Wraps both error and context in a new type | |

**[auto] Selected:** Context in TLS stack only (recommended default)
**Rationale:** FOR-03: "完整的累积链路在作用域内任何 LOG_ERROR 触发时内联序列化，无需手动上下文传递或 spdlog MDC". Zero coupling between error propagation and context accumulation. Least invasive approach.

---

## Claude's Discretion

- **ScopedErrorContext API design** — RAII class mirroring ScopedTimer: move-only, noexcept destructor, placed in `src/logging/logging.h`
- **Snapshot capture function** — `logging::captureEnvironmentSnapshot()` free function, reads atomic/immer state without locks
- **Context depth limit** — Max 16 frames, wraps with `[truncated]` marker
- **Thread safety** — TLS inherently thread-safe; immer reads lock-free; no mutex needed
- **Coexistence with ScopedTimer** — Independent RAII types, can nest/overlap in same scope

## Deferred Ideas

None — discussion stayed within phase scope.
