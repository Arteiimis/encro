# CLI Option Conflict Error Messages Enhancement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move 4 mutual-exclusion flag pairs to CLI11 `->excludes()` at parse time, enhance remaining ad-hoc error prose in config_builder.cpp, and add missing `--pack`/`--pack-only` exclusion.

**Architecture:** Add `excludes`/`excludesDesc` fields to `CmdFlagDef` aggregate, collect exclusion pairs in a `PendingExclusion` vector during flag registration, resolve them against `optRegistry` after all groups are registered. Remove the now-redundant ad-hoc checks from `config_builder.cpp`.

**Tech Stack:** C++26, CLI11, Catch2, clang-cl

---

## Task 1: Write failing exclusion parse tests

**Files:**
- Modify: `tests/cmd_cmd_tests.cpp` — add 4 test cases after the `"reports unknown options"` test (after line 223)

- [ ] **Step 1: Add 4 exclusion rejection test cases**

Insert after line 223 (`}` closing the unknown-options test):

```cpp
TEST_CASE("commandLineInit rejects --flat with --keep", "[cmd]") {
  auto const result = parseArgs({"encro", "--flat", "--keep", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--flat") != std::string::npos);
  CHECK(result.error.value().find("--keep") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --resume with --restart", "[cmd]") {
  auto const result = parseArgs({"encro", "--resume", "--restart", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--resume") != std::string::npos);
  CHECK(result.error.value().find("--restart") != std::string::npos);
}

TEST_CASE("commandLineInit rejects -i with -I", "[cmd]") {
  auto const result = parseArgs({"encro", "-i", "a.mp4", "-I", "b.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("-i") != std::string::npos);
  CHECK(result.error.value().find("-I") != std::string::npos);
}

TEST_CASE("commandLineInit rejects --pack with --pack-only", "[cmd]") {
  auto const result =
    parseArgs({"encro", "--pack", "--pack-only", "-i", "input.mp4"});

  REQUIRE(result.error.has_value());
  CHECK(result.error.value().find("--pack") != std::string::npos);
}
```

- [ ] **Step 2: Build and run the new tests — verify they FAIL**

```bash
cmake --build build && ./build/tests/cmd_cmd_tests "[cmd]" -v
```

Expected: 4 new tests FAIL — CLI11 doesn't know about these exclusions yet.

- [ ] **Step 3: Commit**

```bash
git add tests/cmd_cmd_tests.cpp
git commit -m "test(cmd): add failing exclusion parse tests for 4 flag pairs"
```

---

## Task 2: Add PendingExclusion struct and extend CmdFlagDef

**Files:**
- Modify: `src/cmd/cmd.cpp:391-398` — extend CmdFlagDef with 2 new fields
- Modify: `src/cmd/cmd.cpp` — add PendingExclusion struct before `commandLineInit`

- [ ] **Step 1: Add excludes/excludesDesc to CmdFlagDef**

Replace the existing `CmdFlagDef` struct (lines 391-398):

```cpp
struct CmdFlagDef {
  std::string_view name;  // CLI11-native format: "-v,--verbose" or "--version"
  CmdFlagKind kind;       // Bool | String | Int | SizeT | VecString
  std::string_view description;
  std::string_view
    defaultValue;  // "" → no default (expectedMin=1); non-empty → has default (expectedMin=0)
  int expectedMax;              // 0=flag, 1=single value, >1=multi-value upper bound
  std::string_view excludes;    // flag name this excludes ("" = none); CLI11 ->excludes()
  std::string_view excludesDesc; // custom error message for the exclusion
};
```

- [ ] **Step 2: Add PendingExclusion struct**

Insert before `commandLineInit` function (before line 608):

```cpp
struct PendingExclusion {
  CLI::Option*   option;
  std::string_view targetName;
  std::string_view description;
};
```

- [ ] **Step 3: Commit**

```bash
git add src/cmd/cmd.cpp
git commit -m "feat(cmd): add excludes fields to CmdFlagDef and PendingExclusion struct"
```

---

## Task 3: Wire deferred exclusion resolution into registerFlag

**Files:**
- Modify: `src/cmd/cmd.cpp` — registerFlag lambda + deferred resolution loop

- [ ] **Step 1: Declare pendingExcludes vector and extend registerFlag**

After `std::unordered_map<std::string_view, CLI::Option*> optRegistry;` (line 621), add:

```cpp
std::vector<PendingExclusion> pendingExcludes;
```

In the `registerFlag` lambda, after `if (opt) { optRegistry[def.name] = opt; }` (line 659), add:

```cpp
    if (opt && !def.excludes.empty()) {
      pendingExcludes.emplace_back(opt, def.excludes, def.excludesDesc);
    }
```

- [ ] **Step 2: Add deferred resolution after all groups are registered**

After the FileOpFlags registration loop (after line 679) and before the formatter line (line 682), insert:

```cpp
  // Resolve deferred exclusions
  for (auto const& pe : pendingExcludes) {
    auto it = optRegistry.find(pe.targetName);
    if (it != optRegistry.end()) {
      pe.option->excludes(it->second, std::string{pe.description});
    }
  }
```

- [ ] **Step 3: Build to verify compilation**

```bash
cmake --build build
```

Expected: compiles cleanly (no new tests pass yet — no flag defs have exclusion data).

- [ ] **Step 4: Commit**

```bash
git add src/cmd/cmd.cpp
git commit -m "feat(cmd): wire deferred exclusion resolution into registerFlag"
```

---

## Task 4: Add exclusion data to 4 flag definitions

**Files:**
- Modify: `src/cmd/cmd.cpp` — 4 flag definitions in flag arrays

- [ ] **Step 1: Add exclusion to `--flat`**

Change the `--flat` definition (find `.name = "--flat"`):

```cpp
  CmdFlagDef{
    .name = "--flat",
    .kind = CmdFlagKind::Bool,
    .description = "flatten output names inside the output directory (default)",
    .defaultValue = "",
    .expectedMax = 0,
    .excludes = "--keep",
    .excludesDesc =
      "--flat (default) flattens output names; use --keep to preserve subdirectory structure "
      "instead."
  },
```

- [ ] **Step 2: Add exclusion to `--resume`**

Change the `--resume` definition (find `.name = "--resume"`):

```cpp
  CmdFlagDef{
    .name = "--resume",
    .kind = CmdFlagKind::Bool,
    .description = "resume previous unfinished job state when available",
    .defaultValue = "",
    .expectedMax = 0,
    .excludes = "--restart",
    .excludesDesc =
      "--resume continues a previous job; use --restart to discard state and begin fresh."
  },
```

- [ ] **Step 3: Add exclusion to `-i,--input`**

Change the `-i,--input` definition (find `.name = "-i,--input"`):

```cpp
  CmdFlagDef{
    .name = "-i,--input",
    .kind = CmdFlagKind::String,
    .description = "input file or directory path",
    .defaultValue = "",
    .expectedMax = 1,
    .excludes = "-I,--inputs",
    .excludesDesc =
      "Use -i for a single input path, or -I for multiple input paths — not both."
  },
```

- [ ] **Step 4: Add exclusion to `--pack`**

Change the `--pack` definition (find `.name = "-p,--pack"`):

```cpp
  CmdFlagDef{
    .name = "-p,--pack",
    .kind = CmdFlagKind::Bool,
    .description = "pack encoded video outputs into zip files",
    .defaultValue = "",
    .expectedMax = 0,
    .excludes = "-z,--pack-only",
    .excludesDesc = "--pack encodes then packs; use --pack-only to pack without encoding."
  },
```

- [ ] **Step 5: Build and run the exclusion tests — verify they PASS**

```bash
cmake --build build && ./build/tests/cmd_cmd_tests "rejects --flat" -v && ./build/tests/cmd_cmd_tests "rejects --resume" -v && ./build/tests/cmd_cmd_tests "rejects -i with" -v && ./build/tests/cmd_cmd_tests "rejects --pack" -v
```

Expected: all 4 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/cmd/cmd.cpp
git commit -m "feat(cmd): add CLI11 excludes() for 4 mutual-exclusion flag pairs"
```

---

## Task 5: Remove redundant ad-hoc checks from config_builder.cpp

**Files:**
- Modify: `src/cmd/config_builder.cpp:98-109` — remove flat/keep check from `readOutputLayout`
- Modify: `src/cmd/config_builder.cpp:271-276` — remove resume/restart check from `buildConfig`

- [ ] **Step 1: Remove flat/keep check from `readOutputLayout`**

Replace lines 98-109:

Old:
```cpp
auto readOutputLayout(CmdParseResult const& result) -> eh::Result<appctx::OutputLayout> {
  auto const useFlat = result.flat;
  auto const useKeep = result.keep;

  if (useFlat && useKeep) {
    return eh::makeError("--flat and --keep cannot be used together.");
  }

  if (useKeep) { return appctx::OutputLayout::Keep; }

  return appctx::OutputLayout::Flat;
}
```

New:
```cpp
auto readOutputLayout(CmdParseResult const& result) -> eh::Result<appctx::OutputLayout> {
  if (result.keep) { return appctx::OutputLayout::Keep; }
  return appctx::OutputLayout::Flat;
}
```

- [ ] **Step 2: Remove resume/restart check from `buildConfig`**

Delete lines 271-276 (the check; keep the assignments on 271-272):

Old:
```cpp
  config.resumeState = result.resume;
  config.restartState = result.restart;

  if (config.resumeState && config.restartState) {
    return eh::makeError("--resume and --restart cannot be used together.");
  }
```

New:
```cpp
  config.resumeState = result.resume;
  config.restartState = result.restart;
```

- [ ] **Step 3: Build to verify compilation**

```bash
cmake --build build
```

- [ ] **Step 4: Commit**

```bash
git add src/cmd/config_builder.cpp
git commit -m "refactor(cmd): remove redundant exclusion checks now handled by CLI11"
```

---

## Task 6: Update remaining error prose in config_builder.cpp

**Files:**
- Modify: `src/cmd/config_builder.cpp:307` — `--compress` requires picture
- Modify: `src/cmd/config_builder.cpp:356` — `-I` requires video
- Modify: `src/cmd/config_builder.cpp:360` — `-I` not with `--pack-only`

- [ ] **Step 1: Update `--compress` error**

Line 307, change:
```
"--compress is only supported when --type is picture."
```
to:
```
"--compress is only supported with --type picture."
```

- [ ] **Step 2: Update `-I` requires video error**

Line 356, change:
```
"-I/--inputs is only supported for video type."
```
to:
```
"-I/--inputs is only supported with --type video."
```

- [ ] **Step 3: Update `-I` with pack-only error**

Lines 359-360, change:
```
"-I/--inputs is not supported with pack-only."
```
to:
```
"-I/--inputs cannot be used with --pack-only; use -i for single-input packing."
```

- [ ] **Step 4: Build**

```bash
cmake --build build
```

- [ ] **Step 5: Commit**

```bash
git add src/cmd/config_builder.cpp
git commit -m "refactor(cmd): improve remaining conflict error messages with usage guidance"
```

---

## Task 7: Fix the "parses flag and option values" test

**Files:**
- Modify: `tests/cmd_cmd_tests.cpp:158-189` — split test that sets conflicting flags

- [ ] **Step 1: Replace the conflicting-flags test**

Old test (lines 158-189) sets `--pack --pack-only --resume --restart --flat --keep` all together — now rejected by CLI11 at parse time.

Replace with:

```cpp
TEST_CASE("commandLineInit parses non-conflicting flags and option values", "[cmd]") {
  auto const result = parseArgs(
    {"encro",
     "--yes",
     "--recursive",
     "--force-conflict-handling=n",
     "--folder-summary",
     "--color=always",
     "--verbose",
     "--verbose-echo",
     "--full-progress",
     "--overwrite",
     "--compress"}
  );

  CHECK(result.yesToAll == true);
  CHECK(result.recursive == true);
  CHECK(result.forceConflictHandling == "n");
  CHECK(result.folderSummary == true);
  CHECK(result.color == "always");
  CHECK(result.verbose == true);
  CHECK(result.verboseEcho == true);
  CHECK(result.fullProgress == true);
  CHECK(result.overwrite == true);
  CHECK(result.compress == true);
}
```

- [ ] **Step 2: Build and run the test**

```bash
cmake --build build && ./build/tests/cmd_cmd_tests "parses non-conflicting flags" -v
```

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/cmd_cmd_tests.cpp
git commit -m "test(cmd): fix flags test — split conflicting pairs into separate exclusion tests"
```

---

## Task 8: Update config_builder tests

**Files:**
- Modify: `tests/cmd_config_builder_tests.cpp` — remove 2 tests, update error strings

- [ ] **Step 1: Remove the flat+keep rejection test**

Delete lines 199-212 (the `"buildConfig rejects conflicting flat and keep"` test case).

- [ ] **Step 2: Remove the resume+restart rejection test**

Delete lines 241-261 (the `"buildConfig rejects conflicting resume and restart"` test case).

- [ ] **Step 3: Update `--compress` error string assertion**

Find the test that checks `"--compress is only supported when --type is picture."` and update the assertion string to `"--compress is only supported with --type picture."`.

- [ ] **Step 4: Update `-I` requires video error string assertion**

Find any test asserting `"-I/--inputs is only supported for video type."` and update to `"-I/--inputs is only supported with --type video."`.

- [ ] **Step 5: Update `-I` with pack-only error string assertion**

Find any test asserting `"-I/--inputs is not supported with pack-only."` and update to `"-I/--inputs cannot be used with --pack-only; use -i for single-input packing."`.

- [ ] **Step 6: Run config_builder tests**

```bash
cmake --build build && ./build/tests/cmd_config_builder_tests -v
```

Expected: all remaining tests PASS.

- [ ] **Step 7: Commit**

```bash
git add tests/cmd_config_builder_tests.cpp
git commit -m "test(cmd): remove redundant exclusion tests, update error string assertions"
```

---

## Task 9: Full build and test verification

- [ ] **Step 1: Clean rebuild**

```bash
cmake --build build --clean-first
```

Expected: zero compilation errors.

- [ ] **Step 2: Run full test suite**

```bash
./build/tests/cmd_cmd_tests -v && ./build/tests/cmd_config_builder_tests -v
```

Expected: all tests PASS.

- [ ] **Step 3: Commit any remaining changes**

```bash
git status
```

If clean, done. Otherwise commit remaining files.
