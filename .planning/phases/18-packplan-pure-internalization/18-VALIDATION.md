---
phase: 18
slug: packplan-pure-internalization
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-04
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
| 18-01-01 | 01 | 1 | SINK-04 | — | PackPlan not in public header | compilation | `xmake build tests` | ✅ pack_plan_internal.h | ⬜ pending |
| 18-01-02 | 01 | 1 | SINK-04 | — | execute(PackPlan) internal-only | unit | `xmake run tests` | ✅ | ⬜ pending |
| 18-01-03 | 01 | 1 | SINK-04 | — | static_assert removed | unit | `xmake run tests` | ✅ | ⬜ pending |
| 18-01-04 | 01 | 2 | SINK-04 | — | #include "pack/pack.h" compile succeeds | compilation | `xmake build tests` | ❌ W0 (new test) | ⬜ pending |
| 18-01-05 | 01 | 2 | SINK-04 | — | pack::PackPlan unreachable from pack.h | SFINAE compile | `xmake run tests` | ❌ W0 (new test) | ⬜ pending |
| 18-01-06 | 01 | 2 | SINK-04 | — | Full test suite green, zero behavioral change | regression | `xmake run tests` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/pack_plan_boundary_test.cpp` — SFINAE compile-time test for PackPlan boundary
- [x] `src/pack/pack_plan_internal.h` — new internal header (created in task, not pre-existing)
- [ ] Existing test infrastructure covers all phase requirements (pack_service_tests, packer_tests, pack_execute_test, pack_service_mock_tests)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Consumer build isolation | SINK-04 | External consumers not in this repo | Verify pipeline.cpp + video_process.cpp + picture_process.cpp build without PackPlan access |
| Designated initializer pattern preserved | SINK-04 | Code review concern | grep for `PackPlan{` confirms all 16 designated initializer sites unchanged |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
