---
phase: 17
slug: picture-process-leak-elimination
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-04
---

# Phase 17 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 (C++, CMake build) |
| **Config file** | `tests/CMakeLists.txt` |
| **Quick run command** | `cmake --build build && ctest --test-dir build -R "picture-process" --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure` |
| **Estimated runtime** | ~15 seconds (picture subset), ~120 seconds (full suite) |

---

## Sampling Rate

- **After every task commit:** Run `ctest --test-dir build -R "picture-process" --output-on-failure`
- **After every plan wave:** Run `ctest --test-dir build --output-on-failure`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 120 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 17-01-01 | 01 | 1 | SINK-03 | — | N/A | unit | `ctest --test-dir build -R "picture-process"` | ✅ | ⬜ pending |
| 17-01-02 | 01 | 1 | SINK-03 | — | N/A | unit | `ctest --test-dir build -R "picture-process"` | ✅ | ⬜ pending |
| 17-01-03 | 01 | 1 | SINK-03 | — | N/A | full | `ctest --test-dir build --output-on-failure` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `tests/picture/picture_process_tests.cpp` — 520 lines of existing Catch2 tests
- [x] `tests/pack_execute_test.cpp` — Pack execute tests
- [x] `tests/pack_api_standalone_compile_test.cpp` — Compile-time verification

*Existing infrastructure covers all phase requirements.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Compile check: no internal pack includes in picture_process.cpp | SINK-03 | Automated grep | `grep -c "packer.h\|pack_internal.h\|packer_types.h" src/picture/picture_process.cpp` returns 0 |

*All other phase behaviors have automated verification.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 120s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
