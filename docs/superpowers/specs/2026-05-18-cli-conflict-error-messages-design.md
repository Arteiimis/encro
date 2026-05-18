# CLI Option Conflict Error Messages Enhancement — Design

**Date:** 2026-05-18
**Status:** Approved
**Scope:** `src/cmd/cmd.cpp`, `src/cmd/config_builder.cpp`, tests

## Goal

Move mutual-exclusion flag pairs from ad-hoc checks in `config_builder.cpp` to CLI11's built-in `->excludes()` mechanism at parse time, and improve the prose of remaining ad-hoc error messages.

## Approach

CLI11 `excludes()` as the primary strategy:
- Add `excludes` / `excludesDesc` fields to `CmdFlagDef`
- Collect exclusions during flag registration, resolve after all flags are registered (deferred approach handles any registration order)
- Keep non-mutual-exclusion checks (dependencies, conditionals, value validation) in `config_builder.cpp` with improved prose

## Design Decisions

### 1. CmdFlagDef Extension (`src/cmd/cmd.cpp`)

Two new fields:

```cpp
struct CmdFlagDef {
  std::string_view name;
  CmdFlagKind kind;
  std::string_view description;
  std::string_view defaultValue;
  size_t expectedMax;
  std::string_view excludes;      // NEW — flag name this excludes ("" = none)
  std::string_view excludesDesc;  // NEW — custom error message
};
```

Both default to `""`, so existing flag definitions need zero changes.

### 2. PendingExclusion struct

Named struct instead of tuple for readability:

```cpp
struct PendingExclusion {
  CLI::Option*   option;
  std::string_view targetName;
  std::string_view description;
};

std::vector<PendingExclusion> pendingExcludes;
```

### 3. Deferred resolution

In `registerFlag` lambda: if `def.excludes` is non-empty, push onto `pendingExcludes`.
After all flag groups are registered: resolve each entry via `optRegistry` lookup.

```cpp
for (auto const& pe : pendingExcludes) {
  auto it = optRegistry.find(pe.targetName);
  if (it != optRegistry.end()) {
    pe.option->excludes(it->second, std::string{pe.description});
  }
}
```

### 4. Exclusion pairs (4 total)

Only one side needs to declare the exclusion — CLI11 makes it mutual.

| # | Flag A | Flag B | Custom Message |
|---|--------|--------|----------------|
| 1 | `--flat` | `--keep` | `--flat (default) flattens output names; use --keep to preserve subdirectory structure instead.` |
| 2 | `--resume` | `--restart` | `--resume continues a previous job; use --restart to discard state and begin fresh.` |
| 3 | `-i,--input` | `-I,--inputs` | `Use -i for a single input path, or -I for multiple input paths — not both.` |
| 4 | `--pack` | `--pack-only` | `--pack encodes then packs; use --pack-only to pack without encoding.` |

Pair #4 (`--pack` / `--pack-only`) is a **new check** — currently not enforced anywhere.

### 5. Remaining ad-hoc checks (stay in config_builder.cpp)

These are dependencies, conditionals, or value validations — not mutual exclusions:

| Check | Error Message |
|-------|--------------|
| `--compress` requires `--type picture` | `--compress is only supported with --type picture.` |
| `--image-quality` requires `--compress` | `--image-quality requires --compress to be enabled.` |
| `--image-quality` range 2–31 | `--image-quality must be between 2 and 31.` |
| `-I/--inputs` requires video type | `-I/--inputs is only supported with --type video.` |
| `-I/--inputs` not with `--pack-only` | `-I/--inputs cannot be used with --pack-only; use -i for single-input packing.` |
| `--force-conflict-handling` y/n | `--force-conflict-handling must be set to y or n.` |
| Input required | `Input path is required.` |

### 6. Not made exclusive

- `--verbose` / `--verbose-echo` — complementary: `--verbose-echo` changes where verbose output goes, not mutually exclusive with `--verbose`.
- `--pack` / `-I` — `--pack-only` is already excluded from `-I`; `--pack` + `-I` is a valid combination.

## Test Strategy

### Removed from `cmd_config_builder_tests.cpp`

- `"buildConfig rejects conflicting flat and keep"` — exclusion now caught at CLI11 parse phase
- `"buildConfig rejects conflicting resume and restart"` — same

### Updated in `cmd_config_builder_tests.cpp`

- Error string assertions updated to match new prose where changed

### Added to `cmd_cmd_tests.cpp`

4 new parse-level tests via `commandLineInit()`:

| Test | Args |
|------|------|
| `"rejects --flat with --keep"` | `--flat --keep -i a.mp4` |
| `"rejects --resume with --restart"` | `--resume --restart -i a.mp4` |
| `"rejects -i with -I"` | `-i a.mp4 -I b.mp4` |
| `"rejects --pack with --pack-only"` | `--pack --pack-only -i a.mp4` |

Each test asserts the error message contains the custom description string.

## Files Changed

| File | Change |
|------|--------|
| `src/cmd/cmd.cpp` | Add `PendingExclusion` struct; add `excludes`/`excludesDesc` to `CmdFlagDef`; deferred resolution loop; 4 flag defs get new exclusion fields |
| `src/cmd/config_builder.cpp` | Remove flat+keep and resume+restart checks; update remaining error prose |
| `tests/cmd_cmd_tests.cpp` | +4 exclusion parse tests |
| `tests/cmd_config_builder_tests.cpp` | Remove 2 obsolete tests; update error string assertions |
