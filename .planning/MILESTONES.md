# MILESTONES.md

## v1.0 — Compact Progress Mode

**Shipped:** 2026-04-26
**Phases:** 2 | **Plans:** 3 | **Tasks:** 5

### Key Accomplishments

1. Compact progress mode as default — single overall bar replaces per-worker slot bars in batch encoding
2. Single "Packing: X/Y" overall bar replaces per-archive bars in default packing
3. `--full-progress` / `-F` flag restores original multi-bar behavior (opt-in)
4. `--verbose-echo` correctly wins over `--full-progress` (no regression)
5. Cross-subsystem `.compact` field propagation fixed in `selectPackPlanIndexes` for `--full-progress` with job state
6. All PackPlan builders now explicitly set `.compact` — no implicit struct-default reliance

### Delivered

CLI tool for video encoding + zip packing now defaults to compact progress bars. Users see a clean single-bar progress display by default, with `--full-progress` available for detailed per-worker/per-archive bars. All 4 E2E flows verified (default encoding+pack, full-progress encoding+pack, pack-only, picture mode). 876 assertions across 203 test cases pass.

### Known Gaps

- No formal VERIFICATION.md for Phase 01 and Phase 02 (process artifacts, tests compensate)
- Implicit `.compact` default in compress-picture path (`picture_process.cpp:467`)
- Duplicate test case in `tests/pack_service_tests.cpp`

### Archives

- `.planning/milestones/v1.0-ROADMAP.md` — Full milestone roadmap
- `.planning/milestones/v1.0-REQUIREMENTS.md` — Requirements archive
- `.planning/v1.0-MILESTONE-AUDIT.md` — Audit report
