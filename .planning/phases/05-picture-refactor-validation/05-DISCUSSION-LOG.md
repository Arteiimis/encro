# Phase 5: Picture Refactor + Final Validation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions captured in 05-CONTEXT.md — this log preserves the discussion.

**Date:** 2026-04-27
**Phase:** 05-picture-refactor-validation
**Mode:** discuss
**Areas discussed:** addCompressTask extraction approach, toJpgEntryName placement, Test gate methodology

## Gray Areas Identified

### 1. addCompressTask extraction approach
**Context:** Lambda at line 332 captures 5 local variables by reference (`compressedSet`, `tempDir`, `toJpgEntryName`, `ec`, `compressTasks`). Individual typed params per Phase 3 D-02 would mean 5 reference parameters.
**Decision:** Individual typed parameters — follows Phase 3 D-02 and Phase 4 precedent (packSourceEntryChunks takes 7 typed parameters). No context struct.
**Rationale:** Consistent with established pattern. 5 reference parameters is not unwieldy for C++.

### 2. toJpgEntryName placement
**Context:** Pure function at line 322-326 with no captures. Simple `.jpg` extension replacement. Only used in one place.
**Decision:** Static free function in anonymous namespace of picture_process.cpp. No shared utility.
**Rationale:** Follows Phase 3 D-01. Only used in one place — moving to shared utility would be speculative.

### 3. Test gate methodology
**Context:** Phases 3-4 used TDD RED gates. Phase 5 is the final validation — all tests must pass.
**Decision:** TDD RED gate for addCompressTask. toJpgEntryName verified directly (simple). Final full test suite run after both extractions.
**Rationale:** addCompressTask is the higher-risk extraction (5 captures, 2 call sites). toJpgEntryName is simple enough for direct verification.

### 4. zipNameForIndex lambdas
**Context:** Lines 467-474 and 599-606 — already clean 1-line delegations capturing `picturePackNamingState`.
**Decision:** Left as-is per Phase 3 D-03 (2-level nesting acceptable).
**Rationale:** No refactoring needed — these are the pattern we aim for.

## Decisions Carried Forward

From Phase 3 CONTEXT.md:
- D-01: Free functions in anonymous namespace
- D-02: Individual typed parameters (no context structs)
- D-03: Leave 2-level nesting as-is
- D-05: Descriptive camelCase naming, trailing return types
