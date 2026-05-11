# Quick Task 260511-wk8: Centralized CLI Flag Management — Context

**Gathered:** 2026-05-11
**Status:** Ready for planning

<domain>
## Task Boundary

Refactor `src/cmd/cmd.cpp` to centrally manage all 26+ CLI flags/options via data structures (`CmdFlagDef`, per-group arrays), keeping existing CLI parse behavior unchanged. The existing `CMDOption`/`CMDFlags` scaffold (lines 206-237) is replaced with a clean design.

</domain>

<decisions>
## Implementation Decisions

### CmdFlagDef struct — minimal, pure data (5 fields)
```cpp
enum class CmdFlagKind { Bool, String, Int, SizeT, VecString };

struct CmdFlagDef {
  std::string_view name;          // "-v,--verbose" or "--version" (CLI11 native format)
  CmdFlagKind       kind;         // Bool | String | Int | SizeT | VecString
  std::string_view description;
  std::string_view defaultValue;  // "" = no default → min=1; non-empty → min=0
  int               expectedMax;  // 0 = flag (no value), 1 = single value, >1 = multi-value upper bound
};
```

- `name` merges `longName` + `shortName` into CLI11-native format.
- `expectedMin` derived from `defaultValue`: empty → 1, non-empty → 0.
- `group` field removed — group membership expressed by which `constexpr` array the entry lives in.

### Grouping — 4 constexpr arrays (no group field on struct)
```cpp
constexpr auto GeneralFlags    = std::array<CmdFlagDef, 7>{...};
constexpr auto IOFrags         = std::array<CmdFlagDef, 10>{...};
constexpr auto ProcessingFlags = std::array<CmdFlagDef, 7>{...};
constexpr auto FileOpFlags     = std::array<CmdFlagDef, 3>{...};
```
Group descriptions ("General options", "Input/Output options", etc.) remain inline in `commandLineInit()`.

### Registration logic — inside commandLineInit()
Iterate each group array → create CLI11 `add_option_group()` → for each entry: switch on `kind` to call `add_flag()` or `add_option()` with derived `expected()` and `default_str()`. Store `CLI::Option*` in a local `std::unordered_map<std::string_view, CLI::Option*>`.

### Result population — local applyMap
A function-scoped `std::unordered_map<std::string_view, ResultSetter>` maps flag names to lambdas that populate `CmdParseResult` fields. `ResultSetter` = `std::function<void(CmdParseResult&, CLI::Option const*)>`. One entry per flag. Does not pollute `CmdFlagDef`.

### Constraint: zero behavioral change
- `CmdParseResult` fields unchanged.
- CLI11 option groups, descriptions, defaults, `expected()` ranges, help output — identical.
- `formatter_fn` / help rendering unchanged (it reads from CLI11 objects, not from CmdFlagDef).
</decisions>

<specifics>
## Specific Ideas

- Existing `CMDOption` / `CMDFlags` (lines 206-237) removed entirely, replaced by the new design.
- `commandLineInit()` body shrinks from ~155 lines to ~50-60 lines.
- Adding a new flag = 1 entry in the appropriate group array + 1 field in `CmdParseResult` + 1 entry in `applyMap`.

</specifics>

<canonical_refs>
No external specs — requirements fully captured in decisions above.
</canonical_refs>
