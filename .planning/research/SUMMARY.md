# Project Research Summary

**Project:** encrō v1.5 — Pack subsystem naming/grouping/summary abstraction
**Domain:** C++ CLI tool (zip archive batch processor) internal API refactoring
**Researched:** 2026-05-04
**Confidence:** HIGH

## Executive Summary

encrō v1.5 adds a declarative configuration layer to the pack subsystem, replacing implicit conventions and consumer-bypass patterns with first-class strategy types on `PackRequest`. The work is scoped to four tightly-coupled SINK milestones: a `NamingStrategy` enum replacing the brittle `OutputLayout`+`forceConflictHandling` boolean combo (SINK-01), a `GroupingStrategy` enum and `SummaryConfig` struct capturing picture's unique two-layer partitioning and summary/cover-image injection (SINK-02), migration of `picture_process.cpp` from direct `PackPlan` construction to the `pack::execute(PackRequest)` API (SINK-03), and enforcement of `PackPlan` as a purely internal type invisible to consumers (SINK-04).

The recommended approach is a dependency-ordered phase sequence — SINK-01 (NamingStrategy) first because `NamingConfig` is the foundation, SINK-02 (GroupingStrategy + SummaryConfig) second because it builds on the naming infrastructure, SINK-03 (picture leakage elimination) third because it requires both, and SINK-04 (PackPlan internalization) last as a verification cleanup. All new types live in the single public header `pack.h`; no new libraries, build system changes, or compilation units are required. The critical tension between research files — whether to use a single `NamingStrategy` enum or preserve the two-axis `OutputLayout`+`forceConflictHandling` model — is resolved below in favor of the single enum, with specific guardrails from the pitfalls research.

The key risks are: (1) behavioral drift in picture zip entry names breaking resumable job state continuity (mitigated by golden tests committed before implementation), (2) summary entry ordering silently breaking when grouping config is introduced (mitigated by structural `isSummary` flag on `PackEntryInput` rather than fragile string prefix conventions), and (3) the two-layer partitioning logic leaking through a poorly-designed `GroupingStrategy` abstraction (mitigated by using strategy enum values that express intent, not Packer-level parameters). Overall confidence is HIGH — all findings are verified against live source code, PROJECT.md authority, and the existing 945-assertion test suite.

## Key Findings

### Recommended Stack

No new dependencies or build system changes are required. All four SINK features use pure C++ standard library types (`enum class`, `std::optional`, `std::string`, `std::size_t`) and live in the existing `pack.h`/`pack.cpp` files. The existing stack — C++26 via clang-cl, xmake, Catch2 for tests — remains unchanged. This is a pure internal API refactoring with zero external library impact.

**Core technologies:**
- **C++26 `enum class`**: For `NamingStrategy` and `GroupingStrategy` — compile-time exhaustive dispatch, no virtual overhead, self-documenting
- **`std::optional<T>`**: For `entryPrefix`, `baseName`, `summary` sub-struct — nullable config fields without sentinel values; already the established pattern on `PackRequest`
- **Designated initializers**: For `NamingConfig`, `SummaryConfig`, `GroupingStrategy` — existing PackRequest construction pattern; `.field = value` syntax makes field purpose explicit at call sites

**What NOT to use:**
- **No strategy pattern with abstract base + virtual dispatch** — violates zero hot-path overhead principle; enum dispatch resolves at compile time
- **No `std::variant` for naming strategy** — three mutually exclusive config states, not runtime-polymorphic algorithms
- **No DI framework** — constructor injection already sufficient; grouping/naming strategies are data, not services
- **No new library for grouping** — existing `Packer::groupPackEntriesWithSubparts()` already implements the needed algorithms

### Expected Features

**Must have (table stakes) — all map directly to SINK milestones:**
- **NamingStrategy enum on NamingConfig** — 3 modes: `Flat` (no conflict disambiguation), `FlatWithForce` (hash-based disambiguation always), `Keep` (preserve relative directory structure). Replaces `OutputLayout` + `forceConflictHandling` boolean combo. Priority P1.
- **GroupingStrategy on PackRequest** — enum values: `PerSourceDir` (default, simple size-based partitioning), `PerSourceDirKeepTogether` (picture's logical buckets → physical groups with source-dir affinity). Priority P1.
- **SummaryConfig on PackRequest** — `enabled` boolean + configurable `entryPrefix` and `regularPrefix` strings. Replaces picture's hardcoded `"0000__"`/`"1000__"` conventions. Priority P1.
- **Picture leak elimination (SINK-03)** — `picture_process.cpp` removes all 5 internal pack includes (`pack_internal.h`, `packer.h`, `packer_types.h`), constructs `PackRequest` instead of `PackPlan`. Priority P1.

**Should have (consistency/completeness):**
- **PackPlan internalization (SINK-04)** — move `PackPlan` out of consumer-visible headers; verified by compile test. Priority P2 (cleanup, no behavioral change).

**Defer (v2+):**
- CLI flags for prefix configurability (`--summary-prefix`, `--regular-prefix`)
- Pattern-based naming templates (`{dir}_{stem}_{index:04d}{ext}`)
- Multi-level grouping strategies (by file type AND source dir AND size)
- Summary selection strategy customization (smallest/largest/newest instead of first-alphabetically)

### Architecture Approach

The v1.5 architecture adds three new public types (`NamingStrategy` enum, `GroupingStrategy` enum, `SummaryConfig` struct) to the existing single-public-header pattern in `pack.h`. All three types are consumed by the internal `buildMediaPackPlan()` function in `pack.cpp`, which grows a strategy-dispatch switch for naming, a grouping-policy switch for partitioning, and optional summary-entry injection logic. The critical invariant is that after Phase 3, ALL consumers — pipeline, video, and picture — exclusively call `pack::execute(PackRequest)`. No consumer outside `src/pack/` constructs a `PackPlan`, calls `Packer` directly, or includes internal pack headers.

**Major components:**
1. **`pack::NamingStrategy` enum** — 3 values (`Flat`, `FlatWithForce`, `Keep`) describing zip entry name generation policy. Lives in `pack.h`, consumed by `NamingConfig`, dispatched in `buildMediaPackPlan()`.
2. **`pack::GroupingStrategy` enum** — 2 values (`PerSourceDir`, `PerSourceDirKeepTogether`) describing how entries are partitioned into archive groups. Lives in `pack.h`, field on `PackRequest`.
3. **`pack::SummaryConfig` struct** — `enabled`, `entryPrefix`, `regularPrefix` fields. Optional sub-struct on `PackRequest`. When enabled, `buildMediaPackPlan()` injects summary entries per source directory with ordering-prefix source keys.
4. **`buildMediaPackPlan()` (expanded)** — Internal anonymous-namespace function in `pack.cpp` that gains: (a) naming strategy dispatch replacing the `layout`+`forceConflictHandling` conditional, (b) grouping strategy dispatch driving `groupPackEntriesWithSubparts()` vs logical-partitioning path, (c) summary entry injection when `SummaryConfig::enabled` is true.
5. **`picture_process.cpp` (restructured)** — Removes all internal pack includes. Builds a `PackRequest` with explicit `NamingConfig`, `GroupingStrategy`, and `SummaryConfig` from `AppConfig` fields. Calls `pack::execute(PackRequest)` exclusively. Keeps `planPictureZipEntryNames()` (compress output path names) and `collectFolderSummaryPictures()` (scan logic) — these are picture-specific concerns, not pack concerns.

### Critical Pitfalls

1. **Summary entry ordering breaks when grouping config is introduced** — Summary entries must appear first in every archive group. Currently enforced by fragile string prefix convention (`"0000__"` lexicographically sorts before `"1000__"`). **Prevention:** Add a dedicated `bool isSummary` field to `PackEntryInput` and have `buildMediaPackPlan()` explicitly place summary entries first regardless of sourceKey ordering. Test the ordering invariant explicitly.

2. **Naming strategy enum must express strategy intent, not consumer identity** — The three values (`Flat`, `FlatWithForce`, `Keep`) describe *what naming operation to perform*, not *which consumer is active*. Avoid names like `PictureFlat`/`PictureKeep` that leak consumer knowledge into the pack module. **Prevention:** Validate that enum values are usable by all consumers (video, pipeline, directory mode); write tests that exercise every enum value from a non-picture context.

3. **PackPlan must be genuinely invisible to consumers after migration** — If `pack_types.h` still declares `PackPlan` in the public include path, consumers can still `#include` it and the internalization claim is false. **Prevention:** After Phase 3, move `PackPlan` to `pack_internal.h`; delete the public `execute(PackPlan, jobState*)` overload or make it `namespace pack::detail`; verify with compile test that `#include "pack/pack.h"` does not expose `pack::PackPlan`.

4. **Two-layer partitioning logic must NOT leak through PackRequest grouping config** — Picture's `buildPictureLogicalBuckets()` → `buildPictureLogicalParts()` → `Packer::groupPackEntries()` chain is the most complex grouping behavior in the system. The `GroupingStrategy` enum must abstract this as `PerSourceDirKeepTogether` without exposing `keepSourceDirsTogetherWhenTotalFilesExceed` as a raw parameter. **Prevention:** Make the enum values semantic ("keep source dirs together") not mechanical ("threshold = 0"); keep the `kMaxPicturesPerPack = 2000` constant internal to `buildMediaPackPlan()`.

5. **Resumable job state breaks if zip file names change** — The resumable execution system uses zip filenames as stable task identities. If naming changes produce different zip names for the same inputs, `--resume` silently re-processes already-completed archives. **Prevention:** The zip file naming logic (`buildPackZipBaseName()`, `appendOrdinalRangeSuffix()`) is already internal to `pack.cpp` and must NOT change. Only zip *entry* names change (and only the way they are computed, not their final values). Add a golden test for zip file names before migration.

---

## Critical Tension Resolved: NamingStrategy Enum vs. Two-Axis Model

### The Conflict

- **FEATURES.md, STACK.md, ARCHITECTURE.md** recommend a single `NamingStrategy` enum with 3 values (`Flat`, `FlatWithForce`, `Keep`) replacing the `OutputLayout` + `forceConflictHandling` boolean pair.
- **PITFALLS.md (Pitfall 2)** recommends keeping the two-axis model, warning that the 3 values are not independent strategies but "2 axes producing 3 meaningful combinations" and that hardcoding them "forecloses future combinations."

### Resolution: Adopt the Single `NamingStrategy` Enum

**Recommendation:** Use a single `NamingStrategy` enum with three values. Reject the two-axis model.

**Rationale:**

1. **The two-axis model represents invalid states.** The combination `{OutputLayout::Keep, forceConflictHandling=true}` is nonsensical — conflict handling is meaningless when paths are preserved. A two-field encoding creates 4 possible states where only 3 are valid. The single enum makes the invalid state unrepresentable.

2. **The "future combinations" concern is theoretical, not practical.** The 2×2 matrix of (Flat|Keep) × (forceConflictHandling|noForce) yields exactly 3 meaningful combinations today. If a future v2.0 needs a 4th strategy (e.g., "Flat with SHA-256 disambiguation" or "Keep with basename-only at leaf"), adding a 4th enum value is a backward-compatible change. An enum with 4 values is still simpler than two separate fields with interdependent semantics.

3. **Consumers benefit from explicit intent.** `NamingStrategy::FlatWithForce` declares "I want hash-disambiguated flat names" directly. The two-field equivalent `{Flat, forceConflictHandling=true}` requires the reader to know that `forceConflictHandling` is a modifier on `Flat` specifically. The enum is self-documenting.

4. **Single switch dispatch is simpler than nested branching.** The current code has `if (layout == Flat) { if (force) { ... } else { ... } } else { ... }`. The enum version is a flat `switch(strategy) { case Flat: ...; case FlatWithForce: ...; case Keep: ...; }`. Fewer branches, easier to test exhaustively.

**Guardrails (from PITFALLS research):**

- **Name values after operations, not consumers.** Use `Flat`, `FlatWithForce`, `Keep` — NOT `PictureFlat`, `PictureKeep`. Every enum value must be meaningful for all consumers (video, pipeline, directory mode).
- **Keep `AppConfig` with its original fields.** `AppConfig` continues to store `OutputLayout` and `forceNameConflictHandling` for CLI parsing. Consumers translate at the call site: `AppConfig → NamingStrategy` conversion is a one-line mapping function, not a pack module concern.
- **Validate with exhaustive tests.** Test all 3 enum values across all 3 consumer modes. Future enum additions must pass this cross-product test.
- **Document the mapping explicitly.** In `NamingConfig`'s doc comment: "Correspondence: Flat = old {Flat, forceConflictHandling=false}, FlatWithForce = old {Flat, forceConflictHandling=true}, Keep = old {Keep, any}."

**Enum values (final recommendation — ARCHITECTURE.md naming):**

```cpp
enum class NamingStrategy {
    Flat,           // Flatten filenames to basename only; no collision disambiguation
    FlatWithForce,  // Flatten with hash-based conflict disambiguation always applied
    Keep,           // Preserve relative directory structure as zip entry names
};
```

**(Alternative naming from FEATURES.md — equally valid):** `FlatBasename`, `FlatHashAlways`, `PreserveRelative`. Slightly more descriptive but longer. Either set is acceptable; the critical property is that values describe *naming operations*, not *consumer identities*.

**Why the two-axis model is rejected (not just "deferred"):**

Keeping `OutputLayout` + `forceConflictHandling` alongside the new `NamingStrategy` enum would create TWO sources of truth for the same behavior. Consumers would need to know which field takes precedence. The deprecation path would be more complex than the direct migration. The cost of the clean break (3 consumer call sites to update) is lower than the cost of maintaining dual representations.

---

## Implications for Roadmap

Based on research, the dependency graph mandates this phase order. SINK-01 must precede SINK-02 (naming is the foundation grouping builds on). SINK-03 requires both SINK-01 and SINK-02 (picture needs NamingStrategy, GroupingStrategy, and SummaryConfig to construct PackRequest). SINK-04 is a verification-only cleanup after SINK-03.

### Phase 1: Naming Strategy Enum + NamingConfig Migration (SINK-01)

**Rationale:** `NamingStrategy` is the foundational type. It must exist before `NamingConfig` can use it, and `NamingConfig` must exist before `PackRequest` can reference it. This phase delivers the naming abstraction without touching any consumer's behavior — internal dispatch changes only. It's the lowest-risk phase and unblocks all subsequent work.

**Delivers:** `NamingStrategy` enum in `pack.h`, modified `NamingConfig` (drops `layout`+`forceConflictHandling`, adds `strategy`+`entryPrefix`), updated `buildMediaPackPlan()` dispatch, updated `buildDirectoryPackPlan()` in `packer.cpp`, updated `pipeline.cpp` (translates `AppConfig` → `NamingStrategy`).

**Addresses:** FEATURES.md SINK-01 (NamingStrategy enum), the resolved tension decision above.

**Uses:** C++26 `enum class`, designated initializers (existing pattern).

**Avoids:** Pitfall 2 (enum explosion — mitigated by operation-not-identity naming), Pitfall M4 (forceConflictHandling default discrepancy — pipeline explicitly sets strategy from AppConfig, no default reliance).

### Phase 2: GroupingStrategy + SummaryConfig on PackRequest (SINK-02)

**Rationale:** These are PackRequest extensions that depend on Phase 1 only for the `pack.h` header structure. GroupingStrategy captures picture's two-layer partitioning as a declarative enum value. SummaryConfig captures the summary/cover-image injection as a toggle with configurable prefixes. Together they make `PackRequest` capable of expressing everything picture currently does via direct PackPlan construction.

**Delivers:** `GroupingStrategy` enum, `SummaryConfig` struct, new fields on `PackRequest` (`groupingStrategy`, `summary`), expanded `buildMediaPackPlan()` with grouping dispatch and summary injection, ported `buildPictureLogicalBuckets()`/`buildPictureLogicalParts()` logic behind `PerSourceDirKeepTogether`.

**Addresses:** FEATURES.md SINK-02 (GroupingStrategy + Summary toggle), the `GroupingStrategy` column in the Competitor Feature Analysis.

**Avoids:** Pitfall 1 (summary ordering — use structural `isSummary` flag, not string prefix convention), Pitfall 4 (two-layer partitioning leak — `PerSourceDirKeepTogether` is semantic, not a raw Packer parameter).

### Phase 3: Picture Process Leak Elimination (SINK-03)

**Rationale:** Only possible after Phase 1+2 are complete. Picture needs `NamingStrategy`, `GroupingStrategy`, and `SummaryConfig` to express its current behavior via `PackRequest`. This is the highest-risk phase due to behavioral drift risk, but also the highest-value — it eliminates 5 internal pack includes from picture_process.cpp and unifies all consumers on the single `pack::execute(PackRequest)` entry point.

**Delivers:** Restructured `picture_process.cpp`: removed `pack_internal.h`/`packer.h`/`packer_types.h` includes, replaced `buildPicturePackPlan()` with `PackRequest` construction, unified compress and non-compress paths through single `pack::execute()` call. All 945 existing assertions pass with zero behavioral change.

**Addresses:** FEATURES.md SINK-03 (Picture leak elimination), the "Anti-Features" entry on consumer-constructed PackPlan.

**Avoids:** Pitfall 5 (behavioral drift — golden tests committed before implementation), Pitfall 6 (collisionnaming scope leak — verified via removed includes), Pitfall 8 (compress/non-compress divergence — single `buildPicturePackRequest()` factory), Pitfall M3 (packAllPicsToZip overlooked — migrated first as simplest case), Pitfall M1 (callback lifetime — picture does NOT use `entryNameForFile`, it pre-builds names into `PackEntryInput`).

### Phase 4: PackPlan Pure Internalization (SINK-04)

**Rationale:** After Phase 3, zero consumers outside `src/pack/` use `PackPlan`. This phase makes it formally internal — mainly a verification step with minimal code changes. The heavy lifting was done in Phase 3.

**Delivers:** `PackPlan` moved from `pack_types.h` to `pack_internal.h` (or a new `pack_detail.h`), public `execute(PackPlan, jobState*)` overload removed or restricted to `detail` namespace, compile test updated to assert PackPlan invisibility, `static_assert(std::is_aggregate_v<PackPlan>)` removed (no longer needed).

**Addresses:** FEATURES.md SINK-04 (PackPlan internalization).

**Avoids:** Pitfall 3 (PackPlan still leaks — compile test enforces invisibility), Pitfall 7 (resumable state — zip file names unchanged, only zip entry names potentially affected and those are verified golden in Phase 3).

### Phase Ordering Rationale

- **Dependency chain is strict:** `NamingStrategy` → `NamingConfig` → `PackRequest` → `buildMediaPackPlan()` → `pack::execute()`. SINK-01 must be first because the type declaration order is non-negotiable in C++.
- **SINK-02 can conceptually overlap with SINK-01** (they use independent fields on PackRequest), but sequencing after is safer: the naming strategy enum and modified NamingConfig are the foundation that GroupingStrategy + SummaryConfig build on in `buildMediaPackPlan()`.
- **Phase 3 is the integration point** where all the new abstractions prove their worth. It should NOT be attempted before Phase 1+2 deliver a complete PackRequest surface that covers picture's full behavior.
- **Phase 4 is zero-risk cleanup.** Can be done immediately after Phase 3 passes all tests. No behavioral change, no consumer impact.

### Research Flags

**Phases likely needing deeper research during planning:**
- **Phase 2:** The two-layer partitioning logic migration (`buildPictureLogicalBuckets` → `buildMediaPackPlan` behind `PerSourceDirKeepTogether`) has complex sort-order and max-entries-per-part semantics. A dedicated research phase on the exact algorithm equivalence is warranted.
- **Phase 3:** The compress-path `toJpgEntryName()` interaction with the new naming strategy dispatch needs careful analysis. The JPG extension substitution currently happens after naming; must verify it stays decoupled from the pack module's naming concerns.

**Phases with standard patterns (skip research-phase):**
- **Phase 1:** `enum class` dispatch is a well-established C++ pattern. The 3-way mapping from `AppConfig` is mechanical. No domain-specific unknowns.
- **Phase 4:** Pure include hygiene and compile-test maintenance. Zero algorithmic complexity.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | No new dependencies needed. All types are standard C++. Verified against actual codebase and xmake.lua. |
| Features | HIGH | All 4 SINK features defined by PROJECT.md authority. Table stakes/defer decisions verified against codebase and competitor analysis (7-Zip, Info-ZIP). |
| Architecture | HIGH | All findings verified against live source code. Component boundaries, include graphs, and dependency chains confirmed by direct file inspection. Phase ordering validated by type declaration dependencies. |
| Pitfalls | HIGH | 8 critical pitfalls, 4 moderate pitfalls, 5 technical debt patterns — all grounded in specific code locations (line numbers, function names, include paths). Recovery strategies costed. "Looks done but isn't" checklist provided. |

**Overall confidence:** HIGH

All four research files are based on direct inspection of the v1.4 codebase, the PROJECT.md authority document, and the existing 945-assertion test suite. No findings rely on inference or single-source speculation. The one disagreement (NamingStrategy enum vs. two-axis model) has been explicitly resolved above with rationale and guardrails.

### Gaps to Address

- **Summary deduplication behavior:** When the first picture in a source directory is selected as the summary and summary is enabled, a duplicate entry may be created. FEATURES.md flags this as "Nice to Have" but the exact deduplication logic (skip duplicate entirely vs. keep one copy) needs a decision during Phase 2 planning.
- **`collision_naming.h` final location:** Currently in `src/core/`, used by both `pack/` and `picture/`. After Phase 3, picture no longer uses it directly, but `picture_process.cpp` may still need `stablePathString()` for the compress task key computation. If so, those utilities genuinely belong in `core/`. If not, the file could move to `pack/`. A post-Phase 3 audit should determine the final location.
- **`AppConfig` deprecation path for `outputLayout` and `forceNameConflictHandling`:** The long-term question is whether `AppConfig` should eventually adopt `NamingStrategy` directly, eliminating the translation step. Deferred to v1.6+ when CLI flags are reconsidered.
- **Video consumer's `entryNameForFile` callback:** Currently unused by video (it uses `entryInputs` with pre-computed names). If future video features need naming, they should follow the same pattern. Document this design decision in `PackRequest`'s doc comment.

## Sources

### Primary (HIGH confidence)
- **Source code (all files read and verified 2026-05-04):** `src/pack/pack.h`, `src/pack/pack.cpp`, `src/pack/pack_types.h`, `src/pack/packer_types.h`, `src/pack/packer.h`, `src/pack/packer.cpp`, `src/pack/pack_service.h`, `src/pack/pack_internal.h`, `src/picture/picture_process.cpp`, `src/video/video_process.cpp`, `src/app/pipeline.cpp`, `src/core/collision_naming.h`, `src/core/app_context.h` — direct codebase inspection, all findings traceable to specific line numbers
- **`.planning/PROJECT.md`** — v1.4 shipped architecture, v1.5 milestone scope (SINK-01 through SINK-04), v1.4 design decisions (aggregate preservation, encapsulation, zero hot-path overhead)
- **`xmake.lua`** — confirmed dependency list: boost, thread-pool, spdlog, fmt, indicators, immer, libzippp, catch2
- **Test suite:** `tests/pack_execute_test.cpp`, `tests/pack_api_standalone_compile_test.cpp` — verified existing PackRequest patterns and public API boundary enforcement

### Secondary (MEDIUM confidence)
- **libzippp documentation** — Context7 `/ctabin/libzippp` — confirms entry naming is application-level, library stores name strings only
- **7-Zip CLI documentation** — `-j` (junk paths), `-v` (volume splitting) — competitor feature comparison
- **Info-ZIP (zip) man page** — `-j` (junk paths), `-s` (split size) — competitor feature comparison

### Tertiary (LOW confidence)
- **C++ general design patterns** — training data (std::function lifetime, optional explosion, enum design) — validated against codebase patterns, not independently sourced

---

*Research completed: 2026-05-04*
*Ready for roadmap: yes*
