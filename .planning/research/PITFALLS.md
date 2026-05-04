# Pitfalls Research

**Domain:** C++ pack/archive API refactoring — adding naming strategy abstraction + grouping config to existing 3-consumer system
**Researched:** 2026-05-04
**Confidence:** HIGH

## Critical Pitfalls

### Pitfall 1: Summary Entry Ordering Breaks When Grouping Config Is Introduced

**What goes wrong:**
Picture's summary feature depends on a fragile implicit ordering contract: summary entries must be placed first in each logical bucket, and the `sourceKey` prefix `"0000__"` ensures lexicographic ordering. When grouping configuration is added (e.g., `keepSourceDirsTogetherWhenTotalFilesExceed` param exposed, or reordering logic touches groups), the summary-first invariant can silently break. The zip output will still be valid but summary entries may appear mid-archive or after non-summary files.

**Why it happens:**
The ordering depends on two mechanisms working in tandem: (a) `buildPictureLogicalBuckets()` inserts summary entries before iterating regular entries in `buildPictureLogicalParts()`, and (b) `isSummaryPicturePackEntry()` checks `sourceKey->starts_with("0000__")` to identify summaries. If the grouping config abstraction changes either the sourceKey generation or the ordering within `groupPackEntries`, summaries slide out of position. This is a *temporal coupling* between naming strategy and grouping config — they are implemented as independent abstractions but share a hidden invariant.

**How to avoid:**
1. **Enforce summary-first with a structural guarantee, not a string prefix convention**: Add a dedicated `bool isSummary` field to `PackEntryInput` (or use a tagged variant), then have `buildMediaPackPlan()` (in `pack.cpp`) explicitly copy-summary-entries-first regardless of sourceKey ordering.
2. **Test invariant explicitly**: Add a `TEST_CASE` in `packer_tests.cpp` that verifies when summary entries are present, they appear first in every group/partition after `groupPackEntriesWithSubparts`.
3. **Do not couple summary-first to naming strategy**: Summary position is a grouping concern. Keep it in grouping logic, not in naming.

**Warning signs:**
- Summary entries appearing after file entries in zip listing
- Any change to `sourceKey` generation that doesn't preserve `"0000__"` prefix
- Tests that check zip entry names but not entry ordering

**Phase to address:**
Phase 1 (Grouping Config addition to PackRequest) — this is the earliest it can break. Must be tested before any naming strategy change.

---

### Pitfall 2: Naming Strategy Enum Explosion — Binding Enum Values to Consumer-Specific Modes

**What goes wrong:**
Picture has 3 naming modes (Flat, Keep, FlatForce). If the new `NamingStrategy` enum directly encodes these 3 modes, it creates a leaky abstraction: (a) the enum is useless for video consumers (they don't use these modes), (b) the enum is useless for directory pack-only consumers (they use `buildConflictHandledPackEntryName` differently), (c) adding a 4th picture mode requires modifying the shared enum and all consumers' compilation units. The enum should describe *strategy intent*, not *consumer identity*.

**Why it happens:**
The natural temptation is to "codify what picture already does" — but picture's 3 modes are a combination of layout (Keep vs Flat) + forceConflictHandling flag. They are not 3 independent strategies; they are 2 axes (layout × conflict handling) producing 3 meaningful combinations. Hardcoding the 3 combos as enum values forecloses future combinations and forces all consumers to carry unused variants.

**How to avoid:**
1. **Keep `OutputLayout` + `forceConflictHandling` as two independent fields** in `NamingConfig`. The enum already exists. The naming strategy is determined by `(layout, forceConflictHandling)` tuple, not a flat enum. Do not add a separate `NamingStrategy` enum unless it truly orthogonal dimensions.
2. If an enum IS needed (e.g., for `zipNameStrategy` dispatch), make it describe *how to build the entry name*, not *which picture mode is active*. Example: `EntryNameStyle::RelativePath`, `EntryNameStyle::FlatWithPrefix`, `EntryNameStyle::FlatWithCollisionGroup`. These map to the actual naming operations, not to picture modes.
3. **Validate all enum × layout × forceConflictHandling combinations in tests** — there are only 6 possible combos (2 layouts × 2 conflictHandling bools × entry types). Test all, even the "impossible" ones, to catch regressions.

**Warning signs:**
- New enum with values named after picture-specific concepts (e.g., `PictureFlat`, `PictureKeep`)
- `switch(namingStrategy)` in non-picture code paths
- `static_assert` for enum count that needs updating every milestone

**Phase to address:**
Phase 2 (Naming Strategy Abstraction) — define the abstraction before implementing picture migration.

---

### Pitfall 3: PackPlan Still Leaks to Consumers After "Internal-Only" Claim

**What goes wrong:**
The goal is to make PackPlan internal-only (`pack.h` no longer exposes it). But picture_process.cpp currently constructs PackPlan directly in `buildPicturePackPlan()` and passes it to `pack::execute(PackPlan, jobState*)`. If the migration only wraps this into `PackRequest` but leaves PackPlan visible in the header or adds a backdoor getter, PackPlan remains a transitive dependency of every consumer.

**Why it happens:**
There are 4 call sites in picture_process.cpp that use `buildPicturePackPlan()` → `pack::execute(*plan, ...)`. Three of them use the PackPlan overload. The "quick" migration is to build a PackRequest that internally constructs the PackPlan — but if `pack_types.h` keeps declaring PackPlan, consumers can still `#include "pack/pack_types.h"` and see it. The `static_assert(std::is_aggregate_v<PackPlan>)` in `pack_types.h` is a reverse-dependency marker: it exists BECAUSE external code constructs PackPlan. Removing it is a signal that internalization succeeded.

**How to avoid:**
1. **After all consumers migrate to `pack::execute(PackRequest)` only, move PackPlan to `pack_internal.h`** (or a new `pack_detail.h` that is NOT in the public include path). Remove it from `pack_types.h`.
2. **Delete the `execute(PackPlan, jobState*)` public overload** or make it `namespace pack::detail` only. Only `pack::execute(PackRequest)` should be public.
3. **Verification test**: Add a compilation check — `#include "pack/pack.h"` should NOT give access to `pack::PackPlan`. If any consumer outside `src/pack/` uses PackPlan, compilation must fail.
4. **Remove the `static_assert(std::is_aggregate_v<PackPlan>)`** once internalization is complete — it was a guard against external mutation, no longer needed internally.

**Warning signs:**
- `pack_types.h` still contains PackPlan after "done" claim
- Picture (or any consumer) still `#include "pack/pack_types.h"` after migration
- The `execute(PackPlan, jobState*)` overload remains public

**Phase to address:**
Phase 3 (Picture elimination of detail/internal dependencies) — this is the final step after consumer migration.

---

### Pitfall 4: Two-Layer Partitioning Logic Leaks Through PackRequest Grouping Config

**What goes wrong:**
Picture currently has its own two-layer partitioning: (1) logical buckets → (2) physical groups via `packer.groupPackEntries()` with `keepSourceDirsTogetherWhenTotalFilesExceed = 0`. This is the MOST complex grouping behavior in the entire system. If the new grouping config on PackRequest tries to abstract this with simple knobs (e.g., just `maxFilesPerGroup`), the abstraction either:
- (a) Fails to express picture's needs, forcing picture to keep calling Packer directly
- (b) Adds too many parameters, leaking Packer's grouping internals into the public PackRequest

**Why it happens:**
The two-layer partitioning exists because picture has a unique constraint: summary entries from the same source directory must stay with their regular entries in the same logical part. Without this, you'd get summaries for directory B in the middle of directory A's files. The `keepSourceDirsTogetherWhenTotalFilesExceed = 0` parameter means "never split a source directory across packs, even if it means exceeding the file count limit." This is a semantic constraint, not just a size threshold.

**How to avoid:**
1. **Make `keepSourceDirsTogether` a first-class grouping strategy field on PackRequest**, not a hidden optional parameter. Something like:
   ```cpp
   enum class GroupingStrategy {
     Simple,           // groupPackEntries — size-based only
     SourceDirAware,   // groupPackEntriesWithSubparts — keep source dirs intact
   };
   ```
2. **Picture's logical partitioning (buckets → parts) should remain inside the pack module**, driven by `GroupingStrategy::SourceDirAware`. Do not expose `keepSourceDirsTogetherWhenTotalFilesExceed` value directly.
3. **Add `maxEntriesPerLogicalPart` to PackRequest** — picture's `kMaxPicturesPerPack = 2000` constraint must be expressible. This is currently done in `buildPictureLogicalParts()` and needs to become internal logic inside `buildMediaPackPlan()`.
4. **Test that the two-layer partitioning produces identical group/partition counts** before and after migration. Compare `packer.groupPackEntriesWithSubparts()` output for the same inputs.

**Warning signs:**
- PackRequest getting a raw `std::optional<std::size_t> keepSourceDirsTogetherWhenTotalFilesExceed` field (this is a Packer detail)
- Picture code still calling `packer.groupPackEntries()` directly after "migration complete"
- Different group counts for the same picture directory in old vs new code path

**Phase to address:**
Phase 1 (Grouping Config) — must be designed correctly before picture migration.

---

### Pitfall 5: Behavioral Drift From Naming Strategy Callback Migration

**What goes wrong:**
When picture's `planPictureZipEntryNames()` is replaced by `NamingConfig` + `entryNameForFile` callback + `NamingStrategy` enum, the exact entry names produced must be byte-for-byte identical. A mismatch in edge cases (trailing slashes, unicode paths, filenames with dots, relative path normalization) causes existing zip archives to be incompatible with resumable job state, and E2E tests to fail on seemingly unrelated assertions.

**Why it happens:**
Picture's naming logic in `planPictureZipEntryNames()` has several non-obvious behaviors:
1. **Keep layout**: Uses `lexically_relative()` — the root directory is `dirPath`, and entries relative to it. If `dirPath` normalization changes (trailing slash vs no slash), the relative path changes.
2. **Flat layout without conflict**: Uses `buildFlatPictureEntryName()` which hardcodes `"1000__"` prefix — no collision group prefix.
3. **Flat layout with conflict (FlatForce)**: Uses `buildConflictHandledPictureEntryName()` → `collisionnaming::buildConflictHandledFlatName()` which produces `{groupLabel}__{hash}__{stem}__{hash}{ext}`. The group label via `buildCollisionGroupPrefix(dirPath, filePath)` normalizes `dirPath` and `filePath` through `stablePathString()` which lowercases.
4. **Summary entries**: `buildSummaryPictureEntryName()` hardcodes `"0000__summary__{prefix}__{filename}"`.
5. **Compress path additionally calls `toJpgEntryName()`** which replaces the extension with `.jpg`.

If the new `entryNameForFile` callback or naming strategy produces different strings for any of these paths, the zip entry names differ from v1.4, breaking resumable state continuity.

**How to avoid:**
1. **Snapshot reference entry names first**: Write a test that runs `planPictureZipEntryNames()` on a known test directory and captures ALL entry names as golden strings. Commit this test BEFORE starting migration. The migration is complete only when the same inputs produce exactly the same golden strings through the new code path.
2. **Decouple naming from grouping**: `planPictureZipEntryNames()` currently BOTH assigns names AND resolves collisions (grouping same-named files). The migration should separate: naming strategy produces candidate names, Packer's `makeUniqueZipEntryName` handles collisions. But the candidate names must match.
3. **Handle the `preferredEntryName` edge case**: In the current code, when `plannedEntryNames.find(picPath)` returns `end()`, the fallback is `picPath.filename().generic_string()`. The new code path must replicate this exactly including `generic_string()` normalization.
4. **Compress path `toJpgEntryName`**: This is currently applied AFTER naming. Make sure the new abstraction doesn't bake JPG conversion into the naming strategy — it's a compression concern, not a naming concern.

**Warning signs:**
- `entryNameForFile` callback producing filenames that differ from the golden set
- Unicode paths producing different hashes (case folding in `stablePathString`)
- Summary entries getting `1000__` prefix instead of `0000__summary__`

**Phase to address:**
Phase 2 (Naming Strategy) — golden tests must be committed before implementation begins.

---

### Pitfall 6: `collisionnaming` Namespace Scope Leak — Picture Still Depends on It Post-Migration

**What goes wrong:**
Picture_process.cpp currently uses `collisionnaming::stablePathString`, `collisionnaming::buildCollisionGroupPrefix`, `collisionnaming::buildConflictHandledFlatName`, and `collisionnaming::shortPathHash` directly. After naming strategy abstraction, picture should NOT still depend on these functions — the naming logic is now the pack module's responsibility. If picture keeps these includes, it means the abstraction didn't fully internalize naming.

**Why it happens:**
The `using namespace collisionnaming` at line 25 and the `#include "core/collision_naming.h"` at line 5 of picture_process.cpp are transitively justified because picture builds its own entry names. When naming is "internalized", these become dead includes but the compiler won't warn about unused includes. They can persist indefinitely, maintaining a hidden coupling.

**How to avoid:**
1. **Remove `#include "core/collision_naming.h"` from picture_process.cpp as a migration acceptance criterion.** If compilation fails after removal, the naming abstraction is incomplete.
2. **Move collision-naming-dependent types into pack module types**: `sourceKey`, `fileKey` generation should happen inside `buildMediaPackPlan()`, not in picture's `makePictureSummaryPackEntry()` / `makePictureRegularPackEntry()`.
3. **Track includes in the migration checklist**: Each removed `#include` is a verified decoupling.

**Warning signs:**
- `using namespace collisionnaming` still present in picture_process.cpp after "done"
- `collisionnaming::buildCollisionGroupPrefix` called from picture code
- `naming::stablePathString` alias in picture's anonymous namespace still exists

**Phase to address:**
Phase 3 (Picture elimination) — verify includes removed.

---

### Pitfall 7: Resumable Job State Incompatibility From Changed Zip Names

**What goes wrong:**
The resumable execution system (`pack::execute(PackPlan, jobState*)`) stores archive tasks keyed by zip file name (`jobstate::makeArchiveTask(plan.outputDir / zipName, ...)` at pack.cpp:228). If the naming strategy produces a different zip name for the same input set, the job state store won't recognize the archive as already completed. This causes:
- (a) Redundant re-execution of already-packed archives
- (b) Orphaned job state entries that can never be matched
- (c) Silent data loss if the old archive is overwritten with a differently-named new one

**Why it happens:**
The zip file naming (`zipNameForIndex` lambda on `PackPlan`) includes part/subpart indices and ordinal range suffixes via `appendOrdinalRangeSuffix`. If the new naming strategy changes any of: baseName computation, partIndex assignment, subPartIndex assignment, or ordinal range calculation, the resulting zip filenames differ. The resumable store uses zip filenames as the stable identity of an archive task.

**How to avoid:**
1. **Zip name MUST be deterministic from the pack request inputs**: For the same directory, same config, same files, the zip names must be identical. Add a test that builds PackPlan from request twice and asserts `zipNameForIndex(i)` returns the same string both times.
2. **Preserve the existing `buildPicturePackBaseName` / `buildPackZipBaseName` logic** exactly — this is already internalized in `pack.cpp::buildPackZipBaseName()`. Do not change the format `{baseName}_part{X}.{Y}.zip` or `part{X}.zip` patterns.
3. **Ordinal ranges must be computed identically**: `buildGroupOrdinalRanges()` in `pack_internal.h` must produce the same ranges after migration. This depends on group partitioning being identical.

**Warning signs:**
- Resumable tests that previously skipped archives now process them again
- Job state `.json` files contain archive IDs that don't match generated zip names
- `--resume` flag produces duplicate archives with different names

**Phase to address:**
Phase 2 (Naming Strategy) before picture migration — must validate zip name stability.

---

### Pitfall 8: Compress Path vs Non-Compress Path Divergence Post-Abstraction

**What goes wrong:**
Picture has two nearly identical code paths (compress and non-compress) that share 80%+ logic but differ subtly:
- Compress: sourcePath points to compressed file in tempDir, entryName gets `.jpg` extension
- Non-compress: sourcePath is the original file, entryName keeps original extension

After migrating both to `pack::execute(PackRequest)`, future changes to one path but not the other cause silent divergence. A developer adds a naming feature to the compress path, tests pass (compress tests cover it), but the non-compress path produces wrong entry names.

**Why it happens:**
The two paths are currently forced to share logic through `buildPicturePackPlan()` which is called by both. But the naming is applied at the PackEntryInput level BEFORE the shared plan builder. If the new abstraction applies naming at a different level (e.g., inside `buildMediaPackPlan()` via `entryNameForFile` callback), the source of truth splits: compress path provides entryName differently than non-compress.

**How to avoid:**
1. **Ensure `entryNameForFile` produces the same names for original and compressed variants of the same file** — only the sourcePath differs, not the zip entry name. This means the callback must use the original source file's path, not the compressed temp file's path.
2. **Consolidate compress + non-compress into a single `buildPicturePackRequest()` factory** that takes optional compressed-path mapping. Both paths call the same `pack::execute(PackRequest)`. The only difference is whether `entryInputs` use original or compressed source paths.
3. **Add a parameterized test that exercises both paths** with the same input directory and asserts identical zip entry names (differing only in file content, not zip structure).

**Warning signs:**
- Separate `buildPackRequestForCompress()` and `buildPackRequestForNonCompress()` that duplicate naming logic
- `entryNameForFile` callback that depends on whether the file exists at a given path (temp vs original)
- One path getting a new feature that the other doesn't

**Phase to address:**
Phase 3 (Picture migration) — consolidation should happen during migration, not after.

---

## Moderate Pitfalls

### Pitfall M1: `entryNameForFile` Callback Lifetime Issues

**What goes wrong:**
The callback `entryNameForFile` on PackRequest is a `std::function<std::string(fs::path const&)>`. If it captures by reference and the PackRequest outlives the captured variables (common when PackRequest is moved into `execute()`), the callback becomes dangling. Calling it produces UB — typically a crash or garbage zip entry names.

**Why it happens:**
In `buildMediaPackPlan()` (pack.cpp:154), the callback is called on every entry after grouping. The PackRequest is passed by const-ref to `execute()`, then its `entryInputs` are moved/copied. If `entryNameForFile` captured e.g., `const auto& plannedNames` from picture_process.cpp and PackRequest is moved, the reference dangles.

The current picture code avoids this by building names into `PackEntryInput.entry.zipEntryName` BEFORE constructing `PackEntryInput` — the callback is never involved. Post-migration, if picture sets `request.entryNameForFile = [&](...) { ... };`, this becomes a ticking time bomb.

**How to avoid:**
1. **Picture should NOT use `entryNameForFile` callback at all.** Instead, build entry names directly into `PackEntryInput.entry.zipEntryName` before adding to `PackRequest.entryInputs`. This is the pattern already established.
2. If `entryNameForFile` MUST be used, document that it must outlive the PackRequest. Accept by value in appropriate contexts.
3. Add a test that moves a PackRequest with a lambda-capturing callback into execute() and verifies no use-after-free (e.g., with AddressSanitizer).

**Warning signs:**
- `entryNameForFile = [&](...)` (capture by reference)
- PackRequest stored in a local then moved to execute()
- Random zip entry names or crashes in packer when reading entry names

**Phase to address:**
Phase 2 (Naming Strategy) — the callback API definition.

---

### Pitfall M2: `PackRequest` Grows Without Bound — Optional Everything Becomes Unmanageable

**What goes wrong:**
PackRequest currently has 10 fields, 4 of which are `std::optional`. Adding `groupingStrategy`, `summaryConfig`, `namingStrategy` with their own optional sub-structs creates a combinatorial explosion of optional fields. Consumers must understand which combinations are valid. Invalid combinations (e.g., `summaryConfig` with `PackMode::Directory`) are silently ignored rather than rejected at compile time.

**Why it happens:**
Each new feature gets a `std::optional<T>` field. This is the safe choice (backward compatible), but it defers validation to runtime. The API surface becomes: "10 fields, 4 required, 6 optional, some combinations mutually exclusive, others silently ignored." This is hard to test and easy to misuse.

**How to avoid:**
1. **Use `PackMode` to scope valid fields**: `summaryConfig` is only valid for `PackMode::Media`. `groupingStrategy` might be valid for both but with different semantics. Document in the struct, but also **add a `validate()` method or free function** that checks consistency and returns `eh::Result<void>`.
2. **Group related optional fields into sub-structs**: Already done with `NamingConfig`. Do the same for `GroupingConfig` and `SummaryConfig`. A consumer sets `.grouping = GroupingConfig{...}` or leaves it `std::nullopt`.
3. **Limit to one level of nesting**: Don't let sub-struct options themselves have nested optionals. `SummaryConfig` should have a simple `bool enabled = false` + maybe `perSourceDir = true`.

**Warning signs:**
- PackRequest fields exceeding 15
- More than 3 levels of `std::optional` nesting
- "silently ignore" comments in `execute()` body

**Phase to address:**
Phase 1 (Grouping Config) — design PackRequest extension carefully.

---

### Pitfall M3: Picture's `packAllPicsToZip` Legacy Path Gets Ignored

**What goes wrong:**
`packAllPicsToZip()` (line 718-770 of picture_process.cpp) is a standalone function that packs pictures into a single zip directory (no two-layer partitioning, no max file count). It's called from non-CLI contexts (potentially external consumers). If the migration only refactors `runPicturePackWorkflow()`, this function is left using the old PackPlan API while everything else moves to PackRequest. It becomes a permanent anomaly.

**Why it happens:**
This function is the simplest of the 3 picture packing functions — it has no compress path, no resumable execution, no two-layer partitioning. It's easy to overlook because it's at the bottom of the file and uses minimal dependencies. But it still constructs PackPlan directly and passes it to `pack::execute(*plan)`.

**How to avoid:**
1. **Audit ALL callers of `pack::execute(PackPlan, ...)` before starting migration.** There are 4 call sites in picture_process.cpp: 3 from `runPicturePackWorkflow()` + 1 from `packAllPicsToZip()`.
2. **Migrate `packAllPicsToZip()` first** — it's the simplest and validates that the `PackRequest` path works for basic picture packing without complex grouping.
3. **Search for `pack::execute(` across the full codebase** — anything passing a PackPlan is a migration target.

**Warning signs:**
- `pack::execute(*plan, ...)` still in picture_process.cpp after "done"
- `buildPicturePackPlan()` still exists and is called
- grep shows `PackPlan` usage in non-pack source files

**Phase to address:**
Phase 3 (Picture migration) — part of the comprehensive audit.

---

### Pitfall M4: `forceConflictHandling` Default Semantics Change

**What goes wrong:**
In `AppConfig`, `forceNameConflictHandling = true` (default). In `NamingConfig`, `forceConflictHandling = false` (default). If picture migration changes from reading `AppConfig.forceNameConflictHandling` to using `NamingConfig.forceConflictHandling` without carrying the AppConfig default, the effective default changes from `true` → `false` — reversing the conflict handling behavior for all picture processing.

**Why it happens:**
The `NamingConfig` was designed for the Directory mode where `forceConflictHandling = false` is the sensible default (directory trees naturally avoid name conflicts). But picture's Flat mode defaults to `forceConflictHandling = true`. The NamingConfig default was chosen for the majority use case (Directory), not for pictures.

**How to avoid:**
1. **Picture must explicitly set `NamingConfig.forceConflictHandling` from `AppConfig.forceNameConflictHandling`** — never rely on NamingConfig's default.
2. **Document the default discrepancy** in `NamingConfig`'s struct comment.
3. **Add a test** that verifies picture Flat mode with `forceNameConflictHandling = true` (the default) produces collision-handled names.

**Warning signs:**
- `request.naming = NamingConfig{.layout = ...}` without `.forceConflictHandling`
- Conflict-handling tests pass with default `false` but fail with `true`
- Non-obvious behavior change surfacing in E2E tests

**Phase to address:**
Phase 2 (Naming Strategy) — config mapping must preserve existing defaults.

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Hardcode `"1000__"` and `"0000__summary__"` prefixes in naming strategy mapping | Fast implementation | Magic strings become undocumented invariants; prefix collision risk if video consumer also uses these | Never — extract to named constants in pack module |
| Keep `collisionnaming` namespace import in picture_process.cpp "just for stablePathString" | Avoid extra refactoring | Breaks the "naming is internal to pack" invariant; picture remains coupled to naming implementation details | Only if `stablePathString` is genuinely a core utility (it is), but then it must move to a non-pack, non-picture utility header |
| Add `summaryEnabled` bool directly to `PackRequest` instead of a `SummaryConfig` sub-struct | One less struct to define | Forces all consumers to know about summary concept; breaks separation of concerns | Never — summary is picture-specific, not universal |
| Use `std::function` for grouping strategy instead of enum + dispatch table | Flexibility | No compile-time validation of valid strategies; type erasure hides intent | Only if grouping strategies need runtime composition (they don't here) |
| Copy `buildPicturePackBaseName` logic into new naming strategy enum handler | One less function to extract | Duplicated zip naming logic — change one, forget the other | Never — single source of truth for zip name format |

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| picture → pack::execute() | Passing AppConfig raw instead of translating to NamingConfig+GroupingConfig | Picture builds a PackRequest with explicit config mapping; no AppConfig types cross the boundary |
| video → pack::execute() | Setting `entryNameForFile` callback that captures video-specific state by reference | Video should use `entryInputs` with pre-computed names, not callbacks (same pattern as current code) |
| pipeline → pack::execute() | Different PackRequest construction for video vs picture vs directory modes | All modes use `pack::execute(PackRequest)`, mode-switching logic in pipeline, not in pack module |
| pack::internal → consumers | Accidentally exposing `packer_types.h` types through PackRequest fields | PackRequest only uses types from `pack.h` and `pack_types.h` (public headers); never include `packer_types.h` or `pack_internal.h` in public API |
| collisionnaming → multiple modules | Pack and picture both `using namespace collisionnaming` and calling the same functions from different contexts | After migration, only `pack/` module includes collision_naming.h; picture only calls pack::execute() |

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| `std::function` copy in `buildMediaPackPlan` hot path | Increased memory allocs per archive; slower packing with many small groups | The callback is captured once per PackPlan, copied once per lambda closure — negligible for 1-100 archives. **Not a real concern** but verify with heap profiling if archive count > 1000 | >1000 archive groups |
| `unordered_map` rehashing in `buildPictureLogicalBuckets` | `bucketsByDir.reserve(packInputs.size())` already prevents most rehashing | Existing code is already optimized. New grouping must maintain `.reserve()` calls | Not a concern — already addressed |
| String copying in entry name generation | `std::format` return values copied into `std::string` zipEntryName | Use `std::move` when returning from format. Current code does this implicitly via RVO | Already handled by C++17 guaranteed copy elision |

## "Looks Done But Isn't" Checklist

- [ ] **PackPlan not visible:** `#include "pack/pack.h"` in a test file gives no access to `pack::PackPlan`
- [ ] **No collisionnaming in picture:** `#include "core/collision_naming.h"` removed from `picture_process.cpp`
- [ ] **No Packer direct access:** `#include "pack/packer.h"` removed from `picture_process.cpp`
- [ ] **All 4 picture call sites migrated:** 3 `runPicturePackWorkflow()` + 1 `packAllPicsToZip()` all use `pack::execute(PackRequest)`
- [ ] **Summary ordering preserved:** Summary entries appear first in zip listing for every archive
- [ ] **Golden zip entry names match:** Byte-identical zip entry names for test input directory before vs after migration
- [ ] **Resumable state compatible:** Running `--resume` after interrupted migration build reuses the same zip names
- [ ] **Compile-time validation:** `static_assert` that `PackRequest` works with all 3 modes at compile time (no runtime dispatch needed for basic validation)
- [ ] **`forceConflictHandling` default preserved:** Picture's Flat mode still defaults to `true` (inherited from AppConfig, not NamingConfig default)
- [ ] **Video consumer untouched:** Video's `pack::execute(PackRequest{...})` call still works without any changes (backward compat)

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Summary ordering breaks | MEDIUM | Revert to explicit summary-first insertion in buildMediaPackPlan; add `isSummary` flag to PackEntryInput; re-run golden tests |
| Naming enum explosion | LOW | Revert enum to `OutputLayout` + `forceConflictHandling` tuple; delete enum definition from header (assuming no consumer adopted it yet) |
| PackPlan still leaks | HIGH | Requires touching all consumers' includes; find-replace `pack/pack_types.h` → `pack/pack.h` across codebase; verify compilation |
| Golden entry name mismatch | MEDIUM | Diff golden names vs actual names; fix naming strategy mapping; re-run golden test; if fundamental mismatch, revert naming abstraction |
| Resumable state broken | HIGH | Old job state files are incompatible with new zip names; either provide a migration script or accept that `--resume` from v1.4 requires redoing packing |
| callback lifetime bug | LOW | Change lambda capture from reference to value; add AddressSanitizer test |

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Summary ordering break (P1) | Phase 1 — Grouping Config | Test that summary entries are first in every zip archive listing |
| Enum explosion (P2) | Phase 2 — Naming Strategy | Code review: enum values express strategy intent, not consumer identity |
| PackPlan leak (P3) | Phase 3 — Picture Migration | Compile test: `#include "pack/pack.h"` doesn't expose PackPlan |
| Two-layer partitioning leak (P4) | Phase 1 — Grouping Config | Test identical group/partition counts for same inputs |
| Behavioral drift (P5) | Phase 2 — Naming Strategy | Golden test: byte-identical zip entry names for test directory |
| collisionnaming scope leak (P6) | Phase 3 — Picture Migration | Search: no `collisionnaming` references in non-pack source files |
| Resumable state incompatibility (P7) | Phase 2 — Naming Strategy | Test: `--resume` reuses same zip names |
| Compress/non-compress divergence (P8) | Phase 3 — Picture Migration | Parameterized test: both paths produce identical entry names |

## Sources

- **Codebase analysis:** `src/pack/pack.h`, `src/pack/pack_types.h`, `src/pack/packer.h`, `src/pack/packer_types.h`, `src/pack/pack_internal.h`, `src/pack/pack.cpp`, `src/pack/packer.cpp`, `src/picture/picture_process.cpp`, `src/video/video_process.cpp`, `src/app/pipeline.cpp`, `src/core/collision_naming.h`, `src/core/app_context.h` — HIGH confidence (primary sources)
- **Design patterns:** Refactoring.Guru — Strategy pattern (https://refactoring.guru/design-patterns/strategy) — HIGH confidence (canonical reference)
- **C++ pitfalls:** Training data + codebase analysis — MEDIUM confidence (std::function lifetime, optional explosion, enum design — validated against codebase patterns)
- **v1.4 context:** `.planning/PROJECT.md` — HIGH confidence (official project state)
- **Project principles:** Design decisions document in PROJECT.md (aggregate preservation, encapsulation, zero hot-path overhead) — HIGH confidence

---

*Pitfalls research for: encrō v1.5 — Adding naming strategy abstraction + grouping config to existing pack API*
*Researched: 2026-05-04*
