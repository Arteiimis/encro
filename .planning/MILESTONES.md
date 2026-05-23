# Milestones

## v1.7 日志系统优化 (Shipped: 2026-05-23)

**Phases:** 4 | **Plans:** 13 | **Tasks:** 26 | **Requirements:** 20/20

**Key accomplishments:**
- LOG_* macro layer with automatic source location + module tag injection (24 named async_loggers)
- Per-run timestamped log files + automatic retention cleanup (10 most recent) + crash handler 3-tier fallback
- ScopedTimer RAII stage timing at 7 pipeline boundary sites + ScopedErrorContext error context chains
- NDJSON structured log output via --log-json flag (JsonFormatter using boost::json, dual-sink architecture)
- 340 tests, 3423 assertions, zero regressions

**Archive:** `.planning/milestones/v1.7-ROADMAP.md`, `.planning/milestones/v1.7-REQUIREMENTS.md`

---
