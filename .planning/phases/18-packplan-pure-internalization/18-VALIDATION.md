---
phase: 18
slug: packplan-pure-internalization
status: verified
nyquist_compliant: true
wave_0_complete: true
created: 2026-05-04
validated: 2026-05-04
---

# Phase 18 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3 (`catch2/catch_all.hpp`) |
| **Config file** | `xmake.lua` — `target("tests")` |
| **Quick run command** | `xmake build tests && xmake run tests` |
| **Full suite command** | `xmake build tests && xmake run tests` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build tests && xmake run tests`
- **After every plan wave:** Run `xmake build tests && xmake run tests`
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** ~30 seconds (compile + test)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 18-01-01 | 01 | 1 | SINK-04 | — | PackPlan not in public header | compilation | `xmake build tests` | ✅ pack_plan_internal.h | ✅ green |
| 18-01-02 | 01 | 1 | SINK-04 | — | execute(PackPlan) internal-only | unit | `xmake run tests` | ✅ | ✅ green |
| 18-01-03 | 01 | 1 | SINK-04 | — | static_assert removed | unit | `xmake run tests` | ✅ | ✅ green |
| 18-01-04 | 01 | 2 | SINK-04 | — | #include "pack/pack.h" compile succeeds | compilation | `xmake build tests` | ✅ pack_plan_boundary_test.cpp | ✅ green |
| 18-01-05 | 01 | 2 | SINK-04 | — | pack::PackPlan unreachable from pack.h | SFINAE compile | `xmake run tests "[pack-plan-boundary]"` | ✅ pack_plan_boundary_test.cpp (passes) | ✅ green |
| 18-01-06 | 01 | 2 | SINK-04 | — | Full test suite green, zero behavioral change | regression | `xmake run tests` | ✅ | ✅ green |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `tests/pack_plan_boundary_test.cpp` — SFINAE compile-time test for PackPlan boundary (created in Task 8, passes via `__if_exists`)
- [x] `src/pack/pack_plan_internal.h` — new internal header (created in Task 1)
- [x] Existing test infrastructure covers all phase requirements (pack_service_tests, packer_tests, pack_execute_test, pack_service_mock_tests)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Consumer build isolation | SINK-04 | External consumers not in this repo | ✅ Verified: `xmake build encro` passes (pipeline.cpp + video_process.cpp + picture_process.cpp compile without PackPlan access) |
| Designated initializer pattern preserved | SINK-04 | Code review concern | ✅ Verified: 14 `PackPlan{` sites preserved across pack.cpp(2), pack_service.cpp(1), packer.cpp(1), 4 test files(10) |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** verified 2026-05-04

---

## Validation Audit 2026-05-04

| Metric | Count |
|--------|-------|
| Gaps found | 0 |
| Resolved | 0 |
| Escalated | 0 |

All 6 verification targets satisfied:
- PackPlan not in `pack.h` ✓ (grep confirmed zero hits)
- `pack_plan_internal.h` has PackPlan + execute(PackPlan) ✓
- `static_assert` removed from `pack_types.h` ✓
- `#include "pack/pack.h"` compiles (main app + tests) ✓
- Boundary test (`pack_plan_boundary_test.cpp`) passes ✓
- Full test suite: 3033 assertions in 244 test cases pass ✓
