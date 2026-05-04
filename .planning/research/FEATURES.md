# Feature Research

**Domain:** zip-packing naming strategies, grouping config, and summary toggles for C++ CLI tool
**Researched:** 2026-05-04
**Confidence:** HIGH

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = product feels incomplete.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Keep relative paths in zip | Standard zip behavior; users expect directory structure preserved | LOW | Already exists via `OutputLayout::Keep` + `packer.buildDirectoryPackPlan()` for Directory mode. Picture manually computes `filePath.lexically_relative(dirPath)`. Video uses flat paths. |
| Flatten to filename only | Common when archives aggregate from many sources | LOW | Already exists via `OutputLayout::Flat` in picture_process.cpp. Produces `1000__{filename}` entries. |
| Collision handling in Flat mode | Two files with same name from different dirs must not overwrite | MEDIUM | Already exists: hash-disambiguation via `buildConflictHandledFlatName()` (FNV-1a hash). `forceConflictHandling` config forces hash even without collision. |
| Archive size-bounded grouping | ZIP files must stay under practical limits for distribution | MEDIUM | Already exists: `kDefaultMaxArchiveGroupSize` (500MB), `Packer::groupPackEntries` splits by size. PackService uses across all modes. |
| Entry-name callback injection | Consumers need to control zip entry names for path-only entries | LOW | Already exists: `PackRequest::entryNameForFile` callback. Used when `entryInputs` is empty to apply naming before grouping. |

### Differentiators (Competitive Advantage)

Features that set the product apart. Not required, but valuable.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Naming strategy enum abstraction | Consumers declare intent (`Flat`/`Keep`/`FlatForced`) without implementing naming logic. Replaces 3-way if/else in picture_process.cpp. | MEDIUM | Requires `NamingStrategy` enum + `NamingConfig` extension. Key for SINK-01. Three strategies: `PreserveRelative` (Keep), `FlatBasename` (no-conflict=filename, conflict=hash), `FlatHashAlways` (always hash prefix). |
| Source-directory affinity grouping | Pictures from same source dir stay in same archive even across part splits. Prevents a single directory's files from scattering across 5 different zips. | MEDIUM | Picture already does this via `PictureLogicalBucket` → `buildPictureLogicalParts`. Needs abstraction into PackRequest as `GroupingStrategy` with `maxEntriesPerPart`. Key for SINK-02. |
| Summary/cover image per source directory | First image from each subdirectory placed at zip start with ordering prefix. Enables thumbnail preview without extracting full archive. | MEDIUM | Picture already does this via `collectFolderSummaryPictures` + "0000__" prefix. Needs PackRequest field `includeSummaryPerSourceDir` + configurable `summaryPrefix`. Key for SINK-02. |
| Prefix-controlled zip entry ordering | Predictable ordinal prefix ("0000__" before "1000__") guarantees summary entries appear first when zip extracted alphabetically. | LOW | Currently hardcoded strings in picture_process.cpp. Should be configurable fields on PackRequest: `summaryEntryPrefix`, `regularEntryPrefix`. |
| Declarative single-entry API | Consumers describe intent via one PackRequest struct; all grouping/naming/plan construction internalized. | LOW (already built) | `pack::execute(PackRequest)` exists. Picture still builds PackPlan directly via `buildPicturePackPlan()` — SINK-03 fixes this. |
| Part/subPart zip naming with ordinal ranges | Archives named `part1.zip`, `part1.2.zip` (sub-part) with `[1~50#50p]` ordinal range suffix for clarity. | LOW | Already built in `makeDefaultZipNameStrategy()` + `appendOrdinalRangeSuffix()`. Picture previously had its own `PicturePackNamingState` for this. |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Consumer-constructed PackPlan | Callers want "full control" over grouping | Leaks internal types (PackFileEntry, PackPlan aggregate), breaks encapsulation, prevents future refactoring of internals | PackRequest with strategy enums — picture_process.cpp currently bypasses PackRequest and constructs PackPlan directly (SINK-03 target) |
| Consumer-implemented naming logic | "I know my filenames best" | Duplicates collisionnaming across consumers, hard to test, naming rules drift between modes | NamingStrategy enum on PackRequest — single implementation in pack module |
| Consumer-invoked Packer directly | "Just put these files in a zip" | Bypasses grouping, ordinal ranges, resumable execution | Use `pack::execute(PackRequest)` — Packer is private to PackService |
| Global/hardcoded prefix magic strings | Simplicity | "0000__" and "1000__" cannot change without recompile, can't reuse pack for non-picture archives with different ordering needs | Configurable prefix fields on PackRequest with sensible defaults |
| Regex/pattern-based naming templates | "Rename `{dir}_{stem}_{index:04d}{ext}`" | Massive complexity for marginal utility; users who need this can pre-rename files before packing | Keep naming declarative via strategy enums; pattern templates are a v3 feature at best |
| Per-file custom naming callbacks | Maximum flexibility for edge cases | Runtime callback per file adds overhead, complicates grouping (grouping must know names before partitioning) | `entryNameForFile` already exists for simple path→name mapping; for complex cases use `entryInputs` with pre-computed names |

## Feature Dependencies

```
NamingStrategy enum (SINK-01)
    ├──requires──> NamingConfig extension (existing struct, add strategy field)
    │                 └──requires──> collisionnaming functions (already exist)
    │
    └──consumed by──> buildMediaPackPlan() in pack.cpp (already handles naming, needs strategy dispatch)

GroupingStrategy config (SINK-02)
    ├──requires──> PackRequest extension (new optional field)
    │
    └──consumed by──> buildMediaPackPlan() in pack.cpp (already does grouping, needs affinity param)

Summary toggle (SINK-02)
    ├──requires──> PackRequest extension (summaryEnabled + prefix fields)
    │
    └──consumed by──> buildMediaPackPlan() (must handle summary entry injection per source dir)

Picture leak elimination (SINK-03)
    ├──requires──> SINK-01 (NamingStrategy)
    ├──requires──> SINK-02 (GroupingStrategy + Summary toggle)
    │
    └──enables──> Removal of pack::detail:: and Packer includes from picture_process.cpp

PackPlan pure internalization (SINK-04)
    └──requires──> SINK-03 (Picture uses execute(PackRequest) not execute(PackPlan))
    └──requires──> All 3 consumers use PackRequest exclusively (video already does, pipeline does, picture will after SINK-03)
```

### Dependency Notes

- **SINK-01 must precede SINK-02:** NamingStrategy is simpler (3 enum values, existing NamingConfig base) and unblocks the naming unification. GroupingStrategy + Summary are more complex and can build on the naming work.
- **SINK-02 depends on SINK-01:** GroupingStrategy needs to know naming mode because some grouping decisions interact with naming (e.g., source-dir affinity uses stable path strings as grouping keys, same as collisionnaming).
- **SINK-03 depends on SINK-01 + SINK-02:** Picture cannot move to `pack::execute(PackRequest)` until PackRequest supports summary + grouping + naming strategies that cover picture's current behavior.
- **SINK-04 is a cleanup step:** Once picture stops constructing PackPlan, PackPlan can be moved to an internal header. No behavioral change, just include hygiene.

## MVP Definition

### Must Have for v1.5 (SINK milestones)

- [ ] **NamingStrategy enum on NamingConfig** — 3 modes: `PreserveRelative` (Keep), `FlatBasename` (no-conflict=basename, conflict=hash), `FlatHashAlways` (force hash even without conflict). Replaces current `outputLayout` + `forceConflictHandling` boolean interaction.
- [ ] **GroupingStrategy on PackRequest** — `sourceDirAffinity` boolean + `maxEntriesPerPart` size_t. When true, entries from same sourceDir stay in same logical part (pictures within 2000-count limit).
- [ ] **Summary toggle on PackRequest** — `includeSummaryPerSourceDir` boolean + `summaryEntryPrefix` string ("0000__" default) + `regularEntryPrefix` string ("1000__" default). When enabled, first entry per source dir is duplicated/demoted to a summary entry with ordering prefix.
- [ ] **Picture eliminates pack::detail:: includes** — `runPicturePackWorkflow()` and `packAllPicsToZip()` both use `pack::execute(PackRequest)` directly. No more direct PackPlan construction, no Packer instantiation.

### Nice to Have (v1.5 stretch or v1.6)

- [ ] **Prefix configurability via CLI flags** — `--summary-prefix`, `--regular-prefix` CLI options that feed into PackRequest fields.
- [ ] **Per-mode default naming strategies** — Media mode defaults to Flat, Directory mode defaults to PreserveRelative when `naming` is `std::nullopt`.
- [ ] **Summary entry deduplication** — If a picture is already the first in its source dir AND summary is enabled, don't add a duplicate summary entry (currently it's added separately).

### Out of Scope (intentionally)

- **Pattern-based naming templates** — `{dir}_{stem}_{index:04d}{ext}` syntax. Complexity far exceeds value for CLI batch tool. Users who need this can pre-rename.
- **Multi-level grouping strategies** — grouping by file type AND source dir AND size simultaneously. Current two-level (size + source-dir affinity) suffices.
- **Summary selection strategy customization** — Currently "first file alphabetically." Could become "smallest," "largest," "newest" etc. Defer until user demand.
- **Naming strategy per-entry-type** — Different strategies for summary vs regular entries. Current prefix approach is sufficient and simpler.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| NamingStrategy enum (SINK-01) | HIGH — eliminates 3-way conditional in consumers | MEDIUM — new enum, dispatch in buildMediaPackPlan | P1 |
| GroupingStrategy config (SINK-02) | HIGH — picture's current affinity grouping is WHY it can't use PackRequest | MEDIUM — new optional struct on PackRequest | P1 |
| Summary toggle (SINK-02) | HIGH — picture's summary feature currently requires PackPlan bypass | MEDIUM — injection logic in buildMediaPackPlan | P1 |
| Picture leak elimination (SINK-03) | HIGH — removes 4 internal includes from picture_process.cpp | MEDIUM — restructure picture to build PackRequest | P1 |
| PackPlan internalization (SINK-04) | LOW — consumers don't care, but improves hygiene | LOW — move PackPlan to internal header | P2 |
| CLIC prefix flags | LOW — power users only | LOW — 2 new boost::program_options flags | P3 |
| Summary deduplication | LOW — edge case, cosmetic | LOW — check before inserting | P3 |

## Competitor Feature Analysis

| Feature | 7-Zip CLI | Info-ZIP (zip) | encrō (after v1.5) |
|---------|-----------|----------------|---------------------|
| Preserve paths in zip | Default behavior | `zip -r` preserves tree | NamingStrategy::PreserveRelative |
| Flatten paths | `-j` (junk paths) flag | `-j` flag | NamingStrategy::FlatBasename |
| Conflict handling | Overwrites silently | Overwrites with warning | Hash-disambiguation + force mode |
| Source-dir affinity | Not applicable (handles 1 dir at a time) | Not applicable | GroupingStrategy::sourceDirAffinity |
| Summary/cover image | Not applicable (general-purpose archiver) | Not applicable | Summary toggle with ordering prefix |
| Entry ordering prefix | N/A — entries ordered by addition | N/A — entries ordered by addition | "0000__"/"1000__" prefix convention |
| Size-bounded splitting | `-v` (volume) flag | `-s` (split size) flag | MaxArchiveGroupSize with affinity splitting |

**Key insight:** General-purpose archivers (7-Zip, Info-ZIP) don't have picture-specific features like summary/cover images or source-dir affinity grouping. These are domain-specific differentiators for encrō's batch picture encoding workflow. The table stakes (flat/keep naming, collision handling, size-bounded archives) are universally expected.

## Backward Compatibility Considerations

| Existing Behavior | How It's Preserved | Risk |
|-------------------|-------------------|------|
| `OutputLayout::Flat` + `forceConflictHandling=false` | Maps to `NamingStrategy::FlatBasename` | LOW — identical behavior |
| `OutputLayout::Flat` + `forceConflictHandling=true` | Maps to `NamingStrategy::FlatHashAlways` | LOW — identical behavior |
| `OutputLayout::Keep` | Maps to `NamingStrategy::PreserveRelative` | LOW — identical behavior |
| Video consumer (path-only entries, no summary) | PackRequest defaults unchanged — no summary, no affinity | LOW — video doesn't touch new fields |
| Pipeline consumer (Directory mode) | `PackMode::Directory` path unchanged — uses `packer.buildDirectoryPackPlan()` | LOW — Directory mode isn't modified |
| `NamingConfig::forceConflictHandling` field | Deprecated but still accepted; when `NamingStrategy` is set, it takes precedence | MEDIUM — need deprecation path |
| `AppConfig::forceNameConflictHandling` | Consumers map it to `NamingStrategy` when constructing PackRequest | LOW — mapping in consumer, not pack module |
| `AppConfig::pictureFolderSummary` | Consumers set `PackRequest::includeSummaryPerSourceDir` from it | LOW — mapping in consumer |
| Existing tests (945 assertions) | Zero behavioral change; naming logic moved but produces identical output | LOW — integration tests verify zip contents |

## Sources

- **Codebase analysis:** `src/pack/pack.h`, `src/pack/pack.cpp`, `src/pack/pack_types.h`, `src/pack/packer.h`, `src/pack/packer_types.h`, `src/picture/picture_process.cpp`, `src/core/collision_naming.h`, `src/core/app_context.h` — HIGH confidence (primary sources)
- **libzippp documentation:** Context7 `/ctabin/libzippp` — confirms entry naming is application-level, library just stores name strings — HIGH confidence
- **7-Zip CLI documentation:** `-j` (junk paths), `-v` (volume splitting) — MEDIUM confidence (training data, not fetched live)
- **Info-ZIP (zip) man page:** `-j` (junk paths), `-s` (split size) — MEDIUM confidence (training data)
- **PROJECT.md:** v1.5 milestone scope, SINK-01 through SINK-04 targets — HIGH confidence (project authority)

---

*Feature research for: encrō v1.5 PackRequest naming/grouping/summary abstraction*
*Researched: 2026-05-04*
