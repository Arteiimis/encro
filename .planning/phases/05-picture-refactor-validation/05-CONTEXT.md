# Phase 5: Picture Refactor + Final Validation - Context

**Gathered:** 2026-04-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Extract named lambda variables in `src/picture/picture_process.cpp` (`addCompressTask`, `toJpgEntryName`) to free functions in the anonymous namespace, then run the full test suite as a final validation gate. No behavioral changes — all existing test assertions must pass unchanged.

**Out of scope:** `zipNameForIndex` lambdas (lines 467-474, 599-606) — already clean 1-line delegations following Phase 3 Pattern 3. Left as-is per D-03 precedent.

</domain>

<decisions>
## Implementation Decisions

### Extraction destination
- **D-01:** Extracted functions live as **free functions in the anonymous namespace** of `picture_process.cpp`. Follows Phase 3 D-01 and existing file patterns (lines 28-268 already contain 10+ free functions in anonymous namespace).

### Parameter strategy
- **D-02:** `addCompressTask` receives captured variables as **individual typed references** — following Phase 3 D-02 pattern. 5 reference parameters is consistent with Phase 4 precedent (`packSourceEntryChunks` takes 7 typed parameters). No context struct.

### toJpgEntryName extraction
- **D-03:** `toJpgEntryName` (line 322-326) is a pure function with no captures — trivially extracted to a **static free function** in the anonymous namespace. Only used in one place; no shared utility needed.

### zipNameForIndex treatment
- **D-04:** `zipNameForIndex` lambdas at lines 467-474 and 599-606 are **left as-is**. They are clean 1-line delegations following Phase 3 Pattern 3 (1-line jthread/method delegation). Consistent with D-03 from Phase 3 (2-level nesting is acceptable).

### Test methodology
- **D-05:** **TDD RED gate cycle** for `addCompressTask` extraction — add `REQUIRE(false)` in `tests/picture_process_tests.cpp` before extraction, refactor, then replace with real assertions verifying identical packing behavior.
- **D-06:** `toJpgEntryName` is simple enough to extract and verify directly — no separate RED gate needed. The existing test cases that exercise picture compression implicitly validate it.
- **D-07:** **Final gate:** After both extractions, run the full test suite — all 901 assertions across 214 test cases must pass unchanged.

### the agent's Discretion
- Exact parameter order and naming for `addCompressTask` parameters
- Whether to add explicit test assertions for the extracted function or rely on existing tests
- Placement order of extracted functions within the anonymous namespace

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Primary refactoring target
- `src/picture/picture_process.cpp` — Lines 322-326 (`toJpgEntryName` lambda), 332-346 (`addCompressTask` lambda), 467-474 and 599-606 (`zipNameForIndex` — out of scope, left as-is)

### Header
- `src/picture/picture_process.h` — Public API: `runPicturePackWorkflow`, `packAllPicsToZip`, `buildPicturePackPlan`

### Requirements
- `.planning/REQUIREMENTS.md` — REF-04: Extract `addCompressTask` and `toJpgEntryName` to named functions; REF-05: All 876 assertions pass; REF-06: No behavioral changes
- `.planning/ROADMAP.md` §Phase 5 — Success criteria for extractions + test validation

### Codebase conventions
- `.planning/codebase/CONVENTIONS.md` — Naming (camelCase free functions, trailing return types), formatting (clang-format), anonymous namespace pattern

### Prior phases (established patterns)
- `.planning/phases/03-video-subsystem-refactor/03-CONTEXT.md` — D-01 (anonymous namespace), D-02 (individual typed params), D-03 (leave 2-level as-is)
- `.planning/phases/04-pack-subsystem-refactor/04-01-SUMMARY.md` — Factory function extraction pattern
- `.planning/phases/04-pack-subsystem-refactor/04-02-SUMMARY.md` — Individual typed params with 7 parameters precedent

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Anonymous namespace block** (lines 28-268): Already contains 10+ free functions (`buildFlatPictureEntryName`, `planPictureZipEntryNames`, `buildPicturePackEntryInputs`, etc.). Extracted functions slot naturally into this namespace.
- **`toJpgEntryName` logic**: `std::string const&` → finds last `.`, replaces extension with `.jpg`, or appends `.jpg` if no extension.

### Established Patterns
- **Trailing return type** (`auto fn() -> ReturnType`): All extracted functions must follow this.
- **5 captured references in `addCompressTask`**: `compressedSet` (unordered_map), `tempDir` (fs::path), `toJpgEntryName` (lambda → will become static function), `ec` (std::error_code), `compressTasks` (vector<CompressTask>).
- **`CompressTask` struct**: Defined in `picture_compress.h` — with `inputPath`, `outputPath`, `entryName` fields.

### Integration Points
- **`runPicturePackWorkflow()`** (line 286): The `addCompressTask` lambda is defined inside the `compressImages` branch and called in two loops (lines 348-359). After extraction, the loops call the free function with explicit parameters.
- **Test file**: `tests/picture_process_tests.cpp` — RED gate goes here. Existing tests exercise picture compression workflow.

</code_context>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 05-picture-refactor-validation*
*Context gathered: 2026-04-27*
