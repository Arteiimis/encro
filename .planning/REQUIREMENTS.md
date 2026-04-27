# Requirements: encro

**Defined:** 2026-04-27
**Core Value:** Progress visibility — compact single-bar progress by default

## v1.1 Lambda Readability Refactor

Requirements for eliminating lambda abuse (deep nesting, multi-line inline lambdas) without changing program behavior.

### Lambda Refactoring

- [x] **REF-01**: Extract deeply nested lambdas (3+ levels) in `src/video/video_batch_execution.cpp` to named functions/methods
- [x] **REF-02**: Refactor lambda-wrapping-lambda pattern in `src/pack/pack_service.cpp` (`selectPackPlanIndexes`) to named helpers
- [ ] **REF-03**: Extract inline multi-line lambdas in `src/pack/packer.cpp` (`packSourceEntries`, `spinnerThread`) to named private methods
- [ ] **REF-04**: Extract named lambda variables in `src/picture/picture_process.cpp` (`addCompressTask`, `toJpgEntryName`) to static/named functions

### Constraints

- [ ] **REF-05**: All 876 assertions across 203 test cases pass unchanged
- [ ] **REF-06**: No behavioral changes — only structural code reorganization

## v2 Requirements

(Deferred to future release.)

## Out of Scope

| Feature | Reason |
|---------|--------|
| utils.cpp lambda refactoring | Already well-structured with named lambda variables |
| core/ directory lambdas | Short single-line lambdas for sort/transform, acceptable |
| Tests/ directory lambdas | Test lambdas are appropriately short and readable |
| video_batch_execution.cpp shallow lambdas | Only deeply nested (3+ levels) targeted in REF-01 |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| REF-01 | Phase 3 | Complete |
| REF-02 | Phase 4 | Complete |
| REF-03 | Phase 4 | Pending |
| REF-04 | Phase 5 | Pending |
| REF-05 | Phase 5 | Pending |
| REF-06 | Phase 5 | Pending |

**Coverage:**
- v1.1 requirements: 6 total (2 complete, 4 pending)
- Mapped to phases: 6
- Unmapped: 0 ✓

---

*Requirements defined: 2026-04-27*
*Last updated: 2026-04-27 after initial definition*
