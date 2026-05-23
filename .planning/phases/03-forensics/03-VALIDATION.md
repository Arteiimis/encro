---
phase: 03
name: forensics
date: 2026-05-23
nyquist_version: "1.0"
---

# Phase 03: Forensics - Validation Strategy

## Test Framework

| Property | Value |
|----------|-------|
| Framework | Catch2 v3 (`catch2/catch_all.hpp`, custom runner in `tests/test_main.cpp`) |
| Config file | none -- Catch2 configured via `CATCH_CONFIG_RUNNER` in test_main.cpp |
| Quick run command | `xmake build tests && xmake run tests "[logging]"` |
| Full suite command | `xmake build tests && xmake run tests` |

## Phase Requirements -> Test Map

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

## Sampling Rate

- **Per task commit:** `xmake run tests "[logging]"` -- all logging unit tests
- **Per wave merge:** `xmake run tests` -- full unit + integration suite
- **Phase gate:** Full suite green before `/gsd:verify-work`

## Wave 0 Gaps

- [ ] `tests/logging_error_context_test.cpp` -- covers FOR-01, FOR-03: ScopedErrorContext lifecycle, context chain formatting, frame overflow truncation, LOG_ERROR integration
- [ ] `tests/logging_snapshot_test.cpp` -- covers FOR-02: captureEnvironmentSnapshot format, content verification, null-encoding-state safety
- [ ] Test framework extension: `logging::detail::resetContextStack()` or `ScopedContextReset` RAII fixture to clear TLS stack between test cases (prevents cross-test contamination)
- [ ] Test helper: `registerCapturingLoggerForContext()` -- ostream_sink logger (adapting existing `registerCapturingLoggerForTimer` pattern from `logging_scoped_timer_test.cpp`)
