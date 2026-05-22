---
phase: 2
phase_slug: file-management-observability
date: 2026-05-23
nyquist_version: 1.0
status: ready
---

# Phase 2: File Management + Runtime Observability — Validation Strategy

## Validation Architecture

### Test Suites

| Suite | Tag | Plans | Tests |
|-------|-----|-------|-------|
| File Management | `[logging][file_mgmt]` | 02-01 | Timestamped naming, PID collision, retention cleanup, rotation, fallback chain, currentLogFilePath() |
| ScopedTimer | `[logging][scoped_timer]` | 02-02 | Construction/entry log, destruction/exit log, nesting, move-only, noexcept, steady_clock duration |
| Crash Integration | `[logging][crash_integration]` | 02-03 | Direct file append, 3-tier fallback, format match, edge cases |
| Pipeline Instrumentation | `[video_process]` `[picture]` `[pack_service]` | 02-04 | Video scan/encode/pack timing, picture scan/compress/pack timing, pack timing |

### Aggregated Verification Commands

```bash
# Full Phase 2 verification
xmake build tests && xmake run tests "[logging][file_mgmt]" && xmake run tests "[logging][scoped_timer]" && xmake run tests "[logging][crash_integration]" && xmake build encro && xmake run tests "[video_process]" && xmake run tests "[picture]" && xmake run tests "[pack_service]"
```

### Per-Plan Verification

#### Plan 02-01: File Management Infrastructure
```bash
xmake build tests && xmake run tests "[logging][file_mgmt]"
```

#### Plan 02-02: ScopedTimer
```bash
xmake build tests && xmake run tests "[logging][scoped_timer]"
```

#### Plan 02-03: Crash Handler Integration
```bash
xmake build tests && xmake run tests "[logging][crash_integration]"
```

#### Plan 02-04: Pipeline Instrumentation
```bash
xmake build encro && xmake run tests "[video_process]" && xmake run tests "[picture]" && xmake run tests "[pack_service]"
```

## Validation Dimensions

### Dimension 1: Requirement Coverage
- FILE-01 (per-run files) → 02-01
- FILE-02 (retention cleanup) → 02-01
- FILE-03 (rotating sink) → 02-01
- FILE-04 (crash append) → 02-03
- FILE-05 (directory fallback) → 02-01
- OBS-03 (stage timing) → 02-02, 02-04

### Dimension 2: Success Criteria Coverage
| # | Criterion | Verified By |
|---|-----------|-------------|
| 1 | Timestamped per-run file | 02-01 tests |
| 2 | Auto-cleanup 10 most recent | 02-01 tests |
| 3 | Stage entry/exit with elapsed time | 02-02 + 02-04 tests |
| 4 | Log dir fallback non-blocking | 02-01 tests |
| 5 | Crash diagnostics survive async drain | 02-03 tests |

### Dimension 3: Pitfall Prevention
- Pitfall #4 (clock drift): steady_clock in ScopedTimer, system_clock in spdlog — verified by 02-02 tests
- Pitfall #6 (async drain + rotation): cleanup before creation — verified by 02-01 tests
- Pitfall #10 (crash handler + shutdown): direct file append — verified by 02-03 tests

### Dimension 4: Cross-Plan Integration
- Plan 02-03 depends on Plan 02-01 (currentLogFilePath accessor)
- Plan 02-04 depends on Plan 02-02 (ScopedTimer class)
- Wave 1 (02-01, 02-02) must pass before Wave 2 (02-03, 02-04)

## Notes

- All 4 plans contain well-formed `<verify>` blocks with `<automated>` commands
- No manual verification steps required — all automated via Catch2 test suites
- OBS-03 probe stage merged with scan in Plan 02-04 due to interleaved implementation (noted as WARNING but accepted)

---
*Validation strategy: 2026-05-23*
*Status: ready*
