---
phase: quick
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/picture/picture_process.cpp
autonomous: true
requirements: []

must_haves:
  truths:
    - "runPicturePackWorkflow is ≤20 lines and acts as a pure routing dispatcher"
    - "Compress path behavior is identical to pre-refactor"
    - "Non-compress path behavior is identical to pre-refactor"
    - "No header file is modified (zero external impact)"
    - "All existing picture_process tests pass unchanged"
  artifacts:
    - path: "src/picture/picture_process.cpp"
      provides: "All extracted helper functions in anonymous namespace"
      contains: "executeCompressPackWorkflow"
    - path: "src/picture/picture_process.cpp"
      contains: "executeDirectPackWorkflow"
    - path: "src/picture/picture_process.cpp"
      contains: "buildPackEntryInputs"
    - path: "src/picture/picture_process.cpp"
      contains: "buildPicturePackRequest"
    - path: "src/picture/picture_process.cpp"
      contains: "buildCompressedResultLookup"
  key_links:
    - from: "runPicturePackWorkflow"
      to: "executeCompressPackWorkflow"
      via: "direct call when compressImages is true"
    - from: "runPicturePackWorkflow"
      to: "executeDirectPackWorkflow"
      via: "direct call when compressImages is false"
    - from: "executeCompressPackWorkflow"
      to: "buildCompressedResultLookup"
      via: "call after compressImageBatch returns"
    - from: "executeCompressPackWorkflow"
      to: "buildPackEntryInputs"
      via: "call with compress-path resolver + toJpgEntryName transform"
    - from: "executeDirectPackWorkflow"
      to: "buildPackEntryInputs"
      via: "call with identity resolver + identity transform"
    - from: "executeDirectPackWorkflow / executeCompressPackWorkflow"
      to: "buildPicturePackRequest"
      via: "call with shared parameters"
---

<objective>
Refactor `runPicturePackWorkflow` (~265 lines) into a routing dispatcher (~15 lines) by extracting shared logic and per-path orchestrators into the anonymous namespace.

Purpose: Eliminate code duplication between compress and non-compress paths, improve readability, and establish clear responsibility boundaries — consistent with the v1.1 lambda readability refactor pattern.

Output: A lean `runPicturePackWorkflow` that delegates to `executeCompressPackWorkflow` or `executeDirectPackWorkflow`. Five new helper functions in the anonymous namespace. Zero header file changes.
</objective>

<execution_context>
@C:/Users/LEGION/.config/opencode/get-shit-done/workflows/execute-plan.md
@C:/Users/LEGION/.config/opencode/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/quick/260505-vyf-runpicturepackworkflow/260505-vyf-CONTEXT.md
@src/picture/picture_process.cpp
@src/picture/picture_process.h
@src/picture/picture_compress.h
@src/pack/pack.h
@src/pack/pack_types.h

<interfaces>
<!-- Key types, functions, and patterns the executor needs. Extracted from codebase. -->

From src/picture/picture_process.cpp (anonymous namespace, already exists):
```cpp
using PictureEntryPlan = std::unordered_map<fs::path, std::string>;
constexpr auto kMaxPicturesPerPack = std::size_t{2000};

auto buildSummaryPictureEntryName(fs::path const& dirPath, fs::path const& filePath) -> std::string;
auto toJpgEntryName(std::string const& entryName) -> std::string;
auto buildCompressTaskKey(fs::path const& inputPath, std::string_view entryName) -> std::string;
auto planPictureZipEntryNames(appctx::AppConfig const&, fs::path const&, std::span<fs::path const>) -> PictureEntryPlan;
auto collectFolderSummaryPictures(fs::path const&, std::span<fs::path const>) -> std::vector<fs::path>;
auto addCompressTask(fs::path const& tempDir, std::error_code& ec, std::vector<CompressTask>&, fs::path const&, std::string const&) -> void;
auto confirmPicturePack(appctx::AppConfig const& config) -> bool;
```

From src/picture/picture_process.h (public, DO NOT MODIFY):
```cpp
auto readAllPics(appctx::AppConfig const&, fs::path const&) -> std::vector<fs::path>;
auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath) -> eh::Result<int>;
```

From src/picture/picture_compress.h:
```cpp
struct CompressTask { fs::path inputPath; fs::path outputPath; std::string entryName; };
struct CompressResult { fs::path originalPath; fs::path compressedPath; std::string entryName; };
auto compressImageBatch(appctx::AppContext&, std::span<CompressTask const>, int quality, std::size_t maxParallel) -> std::vector<CompressResult>;
```

From src/pack/pack_types.h:
```cpp
namespace pack {
struct PackFileEntry { fs::path sourcePath; std::string zipEntryName; bool isSummary = false; };
struct PackEntryInput { PackFileEntry entry; fs::path sourceDir; std::optional<std::string> sourceKey; std::optional<std::string> fileKey; bool isSummary = false; };
}
```

From src/pack/pack.h:
```cpp
namespace pack {
enum class NamingStrategy { Flat, FlatWithForce, Keep };
enum class GroupingStrategy { PerSourceDir, PerSourceDirKeepTogether };
enum class PackMode { Media, Directory };
struct NamingConfig { NamingStrategy namingStrategy; std::optional<std::string> baseName; /* ... */ };
struct PackRequest { std::vector<PackEntryInput> entryInputs; PackMode mode; fs::path outputDir; bool compact; bool removeOnFailure; std::optional<NamingConfig> naming; GroupingStrategy groupingStrategy; std::optional<std::size_t> maxParallelJobs; jobstate::Store* jobState; /* ... */ };
auto execute(PackRequest const&) -> eh::Result<PackRunResult>;
}
```

<!-- Key pattern from project: terminal::println(Info/Warning/Success, fmt, args...), eh::makeError(fmt, args...), naming::stablePathString(...) -->
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Extract helper functions and rewrite runPicturePackWorkflow as routing dispatcher</name>
  <files>src/picture/picture_process.cpp</files>
  <action>
Refactor `src/picture/picture_process.cpp` only. Do NOT modify the header, any other `.cpp`, or any test file.

## Changes to make (in order):

### A. Add new functions in the anonymous namespace (between existing helpers and `readAllPics`)

Insert these 5 functions BEFORE line 179 (`auto readAllPics(...`)):

**1. `buildCompressedResultLookup`** — extracts lines 271-278 of current code:
```cpp
auto buildCompressedResultLookup(std::vector<CompressResult> const& compressResults)
    -> std::unordered_map<std::string, fs::path> {
  auto lookup = std::unordered_map<std::string, fs::path>{};
  lookup.reserve(compressResults.size());
  for (auto const& result: compressResults) {
    lookup.emplace(
      buildCompressTaskKey(result.originalPath, result.entryName),
      result.compressedPath
    );
  }
  return lookup;
}
```

**2. `buildPicturePackRequest`** — unifies the two near-identical PackRequest designated-initializer blocks (lines 340-353 and 437-451). The only difference is `baseName`: compress uses `std::string{}`, non-compress uses `dirPath.filename().string()`. Accept `baseName` as a parameter:
```cpp
auto buildPicturePackRequest(
    std::vector<pack::PackEntryInput>&& packInputs,
    fs::path const& outputDir,
    appctx::AppContext const& ctx,
    std::string const& baseName
) -> pack::PackRequest {
  return pack::PackRequest{
    .entryInputs = std::move(packInputs),
    .mode = pack::PackMode::Media,
    .outputDir = outputDir,
    .compact = !ctx.config.fullProgress,
    .removeOnFailure = true,
    .naming =
      pack::NamingConfig{
        .namingStrategy = pack::NamingStrategy::Flat,
        .baseName = baseName,
      },
    .groupingStrategy = pack::GroupingStrategy::PerSourceDirKeepTogether,
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
  };
}
```

**3. `buildPackEntryInputs`** — the parameterized shared function. A `SourceResolver` callable resolves the source path + final entry name for each picture (returns nullopt to skip). An `entryNameTransform` callable handles whether to apply `toJpgEntryName` (compress) or pass through (non-compress):
```cpp
using SourceResolver = std::function<std::optional<
    std::pair<fs::path, std::string>  // {sourcePath, finalEntryName}
>(fs::path const& picPath, std::string const& computedEntryName)>;

auto buildPackEntryInputs(
    std::vector<fs::path> const& summaryPics,
    std::vector<fs::path> const& scannedPics,
    PictureEntryPlan const& plannedEntryNames,
    fs::path const& dirPath,
    SourceResolver const& resolveSource,
    std::function<std::string(std::string const&)> const& entryNameTransform
) -> std::vector<pack::PackEntryInput> {
  auto packInputs = std::vector<pack::PackEntryInput>{};
  packInputs.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    auto const rawEntryName = buildSummaryPictureEntryName(dirPath, summaryPic);
    auto const entryName = entryNameTransform(rawEntryName);
    auto const resolved = resolveSource(summaryPic, entryName);
    if (!resolved) { continue; }
    packInputs.emplace_back(
      pack::PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = resolved->first,
            .zipEntryName = resolved->second,
            .isSummary = true,
          },
        .sourceDir = summaryPic.parent_path(),
        .sourceKey = naming::stablePathString(summaryPic.parent_path()),
        .fileKey = naming::stablePathString(summaryPic),
        .isSummary = true,
      }
    );
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedIt = plannedEntryNames.find(picPath);
    auto const rawEntryName = plannedIt != plannedEntryNames.end()
      ? plannedIt->second
      : picPath.filename().generic_string();
    auto const entryName = entryNameTransform(rawEntryName);
    auto const resolved = resolveSource(picPath, entryName);
    if (!resolved) { continue; }
    packInputs.emplace_back(
      pack::PackEntryInput{
        .entry =
          pack::PackFileEntry{
            .sourcePath = resolved->first,
            .zipEntryName = resolved->second,
          },
        .sourceDir = picPath.parent_path(),
        .sourceKey = naming::stablePathString(picPath.parent_path()),
        .fileKey = naming::stablePathString(picPath),
      }
    );
  }

  return packInputs;
}
```

**4. `executeDirectPackWorkflow`** — extracts the non-compress path (lines 372-462). Uses `buildPackEntryInputs` and `buildPicturePackRequest`:
```cpp
auto executeDirectPackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath, fs::path const& outputDir)
    -> eh::Result<int> {
  auto const scannedPics = readAllPics(ctx.config, dirPath);
  if (scannedPics.empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }

  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouping into package batch(es).",
    terminal::count(scannedPics.size())
  );

  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return 0;
  }

  auto const plannedEntryNames = planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto const resolveSource = [](fs::path const& picPath, std::string const& entryName)
      -> std::optional<std::pair<fs::path, std::string>> {
    return std::pair{picPath, entryName};
  };
  auto const identityTransform = [](std::string const& s) -> std::string { return s; };

  auto const packInputs = buildPackEntryInputs(
    summaryPics, scannedPics, plannedEntryNames, dirPath,
    resolveSource, identityTransform
  );

  auto const request = buildPicturePackRequest(
    std::move(packInputs), outputDir, ctx, dirPath.filename().string()
  );

  auto const packRes = pack::execute(request);
  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
}
```

**5. `executeCompressPackWorkflow`** — extracts the compress path (lines 201-369). Uses `buildCompressedResultLookup`, `buildPackEntryInputs`, and `buildPicturePackRequest`:
```cpp
auto executeCompressPackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath, fs::path const& outputDir)
    -> eh::Result<int> {
  auto const scannedPics = readAllPics(ctx.config, dirPath);
  if (scannedPics.empty()) {
    return eh::makeError("No pictures found in directory: {}", dirPath.string());
  }

  auto const quality = ctx.config.imageQuality.value_or(5);
  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, will be compressed to JPEG (quality={}).",
    terminal::count(scannedPics.size()),
    terminal::count(quality)
  );

  if (!confirmPicturePack(ctx.config)) {
    terminal::println(Warning, "Packing task canceled by user.");
    return 0;
  }

  auto const tempDir = outputDir / ".compress_tmp";
  auto ec = std::error_code{};
  fs::remove_all(tempDir, ec);
  fs::create_directories(tempDir);

  auto summaryPics = std::vector<fs::path>{};
  if (ctx.config.pictureFolderSummary) {
    summaryPics = collectFolderSummaryPictures(dirPath, scannedPics);
  }

  auto const plannedEntryNames = planPictureZipEntryNames(ctx.config, dirPath, scannedPics);

  auto compressTasks = std::vector<CompressTask>{};
  compressTasks.reserve(scannedPics.size() + summaryPics.size());

  for (auto const& summaryPic: summaryPics) {
    auto const entryName = buildSummaryPictureEntryName(dirPath, summaryPic);
    addCompressTask(tempDir, ec, compressTasks, summaryPic, entryName);
  }

  for (auto const& picPath: scannedPics) {
    auto const plannedIt = plannedEntryNames.find(picPath);
    auto const entryName = plannedIt != plannedEntryNames.end()
      ? plannedIt->second
      : picPath.filename().generic_string();
    addCompressTask(tempDir, ec, compressTasks, picPath, entryName);
  }

  terminal::println(
    Info,
    "Compressing {} picture(s) to JPEG (quality={})...",
    terminal::count(compressTasks.size()),
    terminal::count(quality)
  );

  auto const maxParallel = ctx.config.maxParallelJobs.value_or(10);
  auto const compressResults = compressImageBatch(ctx, compressTasks, quality, maxParallel);

  if (compressResults.empty()) {
    fs::remove_all(tempDir, ec);
    return eh::makeError("All picture compressions failed.");
  }

  terminal::println(
    Info,
    "{} picture(s) compressed, preparing pack plan...",
    terminal::count(compressResults.size())
  );

  auto const compressedByTaskKey = buildCompressedResultLookup(compressResults);

  auto const resolveSource = [&compressedByTaskKey](fs::path const& picPath, std::string const& entryName)
      -> std::optional<std::pair<fs::path, std::string>> {
    auto const taskKey = buildCompressTaskKey(picPath, entryName);
    auto const it = compressedByTaskKey.find(taskKey);
    if (it == compressedByTaskKey.end()) { return std::nullopt; }
    return std::pair{it->second, entryName};
  };

  auto const packInputs = buildPackEntryInputs(
    summaryPics, scannedPics, plannedEntryNames, dirPath,
    resolveSource, toJpgEntryName
  );

  if (packInputs.empty()) {
    fs::remove_all(tempDir, ec);
    return eh::makeError("No compressed pictures available to pack.");
  }

  terminal::println(
    Info,
    "Packing {} compressed picture entry(s) into archives...",
    terminal::count(packInputs.size())
  );

  auto const request = buildPicturePackRequest(
    std::move(packInputs), outputDir, ctx, std::string{}
  );

  auto const packRes = pack::execute(request);
  fs::remove_all(tempDir, ec);

  if (!packRes) {
    return eh::makeError("Failed to pack pictures: {}", packRes.error());
  }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
  return 0;
}
```

### B. Replace `runPicturePackWorkflow` body (lines 197-463) with the routing dispatcher:

```cpp
auto runPicturePackWorkflow(appctx::AppContext& ctx, fs::path const& dirPath)
    -> eh::Result<int> {
  auto const outputDir = ctx.config.outputPath.value_or(dirPath) / "packed";

  if (ctx.config.compressImages) {
    return executeCompressPackWorkflow(ctx, dirPath, outputDir);
  }
  return executeDirectPackWorkflow(ctx, dirPath, outputDir);
}
```

### C. Remove the now-unnecessary `using namespace std::literals;` namespace alias (line 21) if it is no longer used by any remaining code. If it is still needed by other functions, leave it.

### Important constraints:
- All new functions go in the anonymous namespace (after existing helpers, before `readAllPics` at line 179)
- No changes to `picture_process.h` or any other file
- Do NOT modify `packAllPicsToZip` (lines 465-556) — leave it unchanged
- The `addCompressTask` and `planPictureZipEntryNames` helpers remain as-is; they are already extracted
- Preserve exact terminal message strings, error messages, and control flow
- The `eh::makeError` and `terminal::println` calls must be byte-for-byte identical to current output
</action>
  <verify>
    <automated>xmake build tests 2>&amp;1 | Select-String -Pattern "error" -NotMatch | Select-String -Pattern "build ok|Build success"</automated>
  </verify>
  <done>
Compilation succeeds with zero errors. All new functions reside in anonymous namespace (between line 26 `namespace {` and line 179 `auto readAllPics`). `runPicturePackWorkflow` is 11 lines (function declaration + body). Original function body (lines 198-463) fully replaced.
</done>
</task>

<task type="auto">
  <name>Task 2: Run picture_process tests to verify zero behavioral change</name>
  <files>src/picture/picture_process.cpp</files>
  <action>
Build and run the full test suite with a focus on picture_process tests to confirm the refactoring introduced no behavioral regression.

1. Build the tests target with clang-cl:
   ```
   xmake build tests
   ```

2. Run the picture_process test suite:
   ```
   xmake run tests -- "[picture_process]"
   ```
   
   If Catch2 tag filtering doesn't match, run with name filter:
   ```
   xmake run tests -- "picture_process*"
   ```
   
   If neither works, fall back to full suite:
   ```
   xmake run tests
   ```

3. Verify all assertions pass. The project has 3033 total assertions across all tests; focus on the picture_process subset but a full run is acceptable.

4. If ANY test fails, analyze the failure, fix the code in `picture_process.cpp` (Task 1 functions only), rebuild, and re-run until all pass.

5. After tests pass, verify the refactored `runPicturePackWorkflow` line count:
   ```
   rg -c "^auto runPicturePackWorkflow" src/picture/picture_process.cpp
   ```
   And check that the full function body from `auto runPicturePackWorkflow(` to the closing `}` is ≤15 lines.
</action>
  <verify>
    <automated>xmake build tests && xmake run tests -- "[picture_process]"</automated>
  </verify>
  <done>
All picture_process tests pass with zero failures. `runPicturePackWorkflow` body is ≤15 lines. The refactored code compiles cleanly with clang-cl. No header files were modified.
</done>
</task>

</tasks>

<verification>
### Overall Verification
1. `xmake build tests` — compiles without errors
2. `xmake run tests -- "[picture_process]"` — all picture_process tests pass
3. `runPicturePackWorkflow` is ≤15 lines (routing dispatcher)
4. `git diff --name-only` shows only `src/picture/picture_process.cpp` changed
5. No header files were modified
6. Anonymous namespace contains all 5 new helper functions + 2 orchestrator functions
</verification>

<success_criteria>
- `runPicturePackWorkflow` is a routing dispatcher of ≤15 lines
- All picture_process tests pass identically to pre-refactor
- Only `src/picture/picture_process.cpp` is modified (zero header changes)
- Five new functions in anonymous namespace: `buildCompressedResultLookup`, `buildPicturePackRequest`, `buildPackEntryInputs`, `executeDirectPackWorkflow`, `executeCompressPackWorkflow`
- Code follows v1.1 lambda readability refactor pattern (extract to anonymous namespace, no header changes)
</success_criteria>

<output>
After completion, create `.planning/quick/260505-vyf-runpicturepackworkflow/260505-vyf-SUMMARY.md`
</output>
