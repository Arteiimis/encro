---
phase: 20
slug: cli-color-deepening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-09
---

# Phase 20 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 v3 |
| **Config file** | none — Catch2 auto-main in tests/test_main.cpp |
| **Quick run command** | `xmake build tests && xmake run tests` |
| **Full suite command** | `xmake build tests && xmake run tests && xmake build e2e_tests && xmake run e2e_tests` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** `xmake run tests "[terminal][cmd]"` — fast terminal + cmd tests
- **After every plan wave:** `xmake run tests` — full unit + integration suite
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 1 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 20-01-01 | 01 | 1 | COLR-02 | T-20-01 | enum values are compile-time constants, no injection path | unit | `xmake run tests "[terminal]"` | ❌ W0 | ⬜ pending |
| 20-02-01 | 02 | 2 | COLR-01 | T-20-04 | option names are compile-time literals, not user input | smoke | `xmake run tests "[cmd]"` | ❌ W0 | ⬜ pending |
| 20-03-01 | 03 | 3 | COLR-04 | T-20-07 | boolean flag, hardcoded output string | integration | `xmake run tests "[cmd]"` | ❌ W0 | ⬜ pending |
| 20-03-02 | 03 | 3 | COLR-03 | N/A | pre-existing — error paths already use println(Error) | smoke | `xmake run tests` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/cmd_cmd_tests.cpp` — extend with: --version flag test, help text smoke test (contains expected strings), NO_COLOR compliance test (no ANSI escape codes in help when DISABLED)
- [ ] `tests/infra/terminal_tests.cpp` — extend with: styleFor() returns non-empty for new MessageKind values, defaultBadgeLabel() returns "" for new kinds, styledText with new kinds in Always/Never modes
- [ ] Framework install: already in place (Catch2 via xmake)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| --help output visual appearance | COLR-01 | ANSI rendering depends on terminal emulator | Run `encro --help` and verify colored output visually |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
