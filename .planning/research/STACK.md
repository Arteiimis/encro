# Stack Research

**Domain:** C++ CLI tool — pack subsystem naming/grouping/summary abstraction layer
**Researched:** 2026-05-04
**Confidence:** HIGH

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| C++26 (clang-cl) | C++26 | Language standard | Already established; `enum class`, `std::optional`, `std::string_view`, designated initializers, `std::expected`-style error handling via `eh::Result` cover all new features |
| xmake | current | Build system | Already established; no new package requirements |
| boost::program_options | current | CLI parsing | Already established; no new CLI flags needed beyond what PackRequest fields absorb |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| *(none new)* | — | — | All new features are pure C++ standard library types |

No new supporting libraries are required. The four v1.5 features are all internal C++ abstractions:

1. **`NamingStrategy` enum class** (`Flat` / `Keep` / `FlatForce`) — standard `enum class`, replaces the implicit `OutputLayout` + `forceNameConflictHandling` boolean combo. Lives in `pack.h`.
2. **`GroupingStrategy`** struct on `PackRequest` — captures `maxEntriesPerGroup` (`std::optional<std::size_t>`) and `sourceDirAffinity` (bool); standard C++ aggregate. Lives in `pack.h`.
3. **`SummaryConfig`** — a boolean `summary` toggle + `std::optional<std::string> summaryPrefix`; standard C++ types. Lives in `pack.h` or as fields directly on `PackRequest`.
4. **`PackPlan` internalization** — architectural change moving `pack_types.h` to internal-only; no new types or libraries.

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| Catch2 | Unit/integration testing | Existing; existing packer + pack-service integration tests continue to cover new grouping/naming paths |
| clang-cl (LLVM) | Compiler | Existing; C++26 `enum class`, `std::optional`, designated initializers all supported |
| xmake | Build orchestration | Existing; no `add_requires()` changes needed |

## Installation

No new dependencies. The existing `xmake.lua` `add_requires()` list is unchanged:

```bash
# No additions — all new features use standard C++ types
# Existing build command unchanged:
xmake build encro
xmake build tests && xmake run tests
```

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| `enum class NamingStrategy` (Flat/Keep/FlatForce) | Keep separate `OutputLayout` + `forceConflictHandling` boolean | Only if backward compat with existing `AppConfig.outputLayout` must be maintained long-term (not the case — v1.5 explicitly abstracts this) |
| `struct GroupingStrategy` aggregate on PackRequest | `std::variant<FlatGrouping, SourceDirGrouping>` discriminated union | Only if grouping strategies diverge into fundamentally different algorithms with incompatible state (not the case — both are configurable via the same max-entries + affinity parameters) |
| `SummaryConfig` as a PackRequest sub-struct | Summary fields flat on PackRequest | Flat fields are simpler for 2 fields; sub-struct only warranted if summary configuration grows beyond 3 fields |
| PackPlan in internal-only header (`pack/pack_internal.h` or new `pack/pack_plan.h`) | PackPlan remains in `pack_types.h` with that header still public | `pack_types.h` must be internal if PackPlan is internal — consumers should never include it. Move PackPlan + PackFileEntry + PackProgressCallbacks to `pack_plan.h` (internal) and keep PackEntryInput in `pack_types.h` only if needed by Packer's internal API |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Boost.DI or any DI framework | Constructor injection already sufficient; PackService owns Packer by value (v1.4 decision). Adding DI for grouping/naming strategies is over-engineering | Direct struct/field assignment on PackRequest |
| Strategy pattern with abstract base + virtual dispatch | Hot-path overhead violates "zero hot-path overhead" principle. Naming strategy is resolved once per archive, not per file | `enum class` switch or `std::function` callback (existing pattern in `NamingConfig::zipNameStrategy`) |
| `std::variant` for naming strategy | Three variants (Flat, Keep, FlatForce) are mutually exclusive config states, not runtime-polymorphic algorithms | `enum class` with 3 enumerators + optional prefix string |
| New library for "grouping strategy" (e.g., range-v3) | Grouping is a simple size+count threshold with optional source-dir affinity; existing `Packer::groupPackEntriesWithSubparts` already implements it | `std::optional<std::size_t> maxEntriesPerGroup` + `bool sourceDirAffinity` on PackRequest |
| `immer` or persistent data structures for naming/grouping config | Config is set once on PackRequest construction, never mutated during execution | Plain C++ value types (bool, enum, optional, string) |
| Exposing `pack::detail::PackEntryInput` in `pack.h` | Would leak internal grouping representation to consumers (the exact problem SINK-03 fixes) | Consumers use `PackRequest::entries` (flat `vector<fs::path>`) and `PackRequest::entryInputs` (already public `PackEntryInput`); internal grouping keys stay in pack.cpp |

## Stack Patterns by Variant

**If naming strategy is Flat:**
- Use `NamingStrategy::Flat` + optional prefix → `buildConflictHandledFlatName` path in `pack.cpp`
- Applies collision prefixes (source-dir group label + hash) to avoid name conflicts
- Same behavior as current `config.outputLayout == Flat && !forceConflictNaming` path

**If naming strategy is FlatForce:**
- Use `NamingStrategy::FlatForce` → ALWAYS applies collision prefixes even for single-file names
- Same behavior as current `config.outputLayout == Flat && config.forceNameConflictHandling` path

**If naming strategy is Keep:**
- Use `NamingStrategy::Keep` → preserves relative directory structure as zip entry names
- Same behavior as current `config.outputLayout == Keep` path

**If grouping strategy specifies sourceDirAffinity:**
- Pass through to `Packer::groupPackEntriesWithSubparts` → `keepSourceDirsTogetherWhenTotalFilesExceed` parameter
- Already implemented in Packer; just needs to be configurable from PackRequest

**If summary is enabled:**
- `pack::execute()` internally calls summary collection (currently `collectFolderSummaryPictures` in picture_process.cpp, moved to `pack.cpp`)
- Summary entries get a dedicated prefix (e.g., `"0000__"`) in their zip entry names
- Summary logic moves from picture_process.cpp consumer into pack subsystem

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| C++26 `enum class` | clang-cl (current) | Fully supported; no ABI concerns since enum is internal to pack subsystem |
| `std::optional<std::string>` | clang-cl (current) | Already used extensively in PackRequest and NamingConfig |
| `PackRequest` designated initializers | clang-cl (current) | Existing pattern; new fields extend the aggregate, backward-compat via default member initializers |

## Sources

- **Codebase analysis** (`src/pack/pack.h`, `src/pack/pack.cpp`, `src/pack/pack_types.h`, `src/pack/packer_types.h`, `src/pack/pack_internal.h`, `src/pack/packer.h`, `src/picture/picture_process.cpp`, `src/core/collision_naming.h`, `src/core/app_context.h`) — HIGH confidence — direct inspection of current types, includes, and dependency graph
- **`.planning/PROJECT.md`** — HIGH confidence — documents v1.4 shipped architecture, v1.5 milestone scope (SINK-01 through SINK-04), and key decisions against framework introduction
- **`xmake.lua`** — HIGH confidence — confirms current dependency list: boost, thread-pool, spdlog, fmt, indicators, immer, libzippp, catch2

---

*Stack research for: encrō v1.5 pack naming/grouping/summary abstraction*
*Researched: 2026-05-04*
