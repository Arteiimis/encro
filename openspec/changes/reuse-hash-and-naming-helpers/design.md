## Context

See `proposal.md` — Why for the four hash wrappers and the two naming planners.

Constraints that shape the extraction:

- `core::collision_naming.h` is included by ten translation units under `src/` (nine directly; `video_encode_runner.cpp` through `segment_dir.h`) and by three test files, and currently carries only light headers (`<algorithm>`, `<cctype>`, `<cstdint>`, `<filesystem>`, `<format>`, `<optional>`, `<string>`, `<string_view>`). A container-based planner added there would put `<unordered_map>` and `<vector>` into all of them.
- House style for callbacks is `std::function<...> const&` — `src/video/video_encode_runner.h:14` defines `using function_ref = std::function<void(std::string const&)> const&;`. `std::function_ref` is not used anywhere in the repo.
- Project language level is C++26 (`xmake.lua:5`), toolchain `clang-cl` on Windows.

The two planners are **not** the same function with one varying callback. They differ in *both* branches:

| | unique branch (`size()==1 && !force`) | conflict branch |
| --- | --- | --- |
| video (`video_output_planning.cpp:142`) | raw `candidatePath` | `candidate.parent_path() / buildConflictHandledFlatName(sourceRootDir, input, stem, ext)` (`:28-39`) |
| picture (`picture_process.cpp:113-116`) | `"1000__" + fileName` | `"1000__" + buildConflictHandledFlatName(dirPath, input, stem, ext)` (`:66-73`) |

Picture wraps the unique case too; video does not. A single "name builder" callback would silently change one of the two.

## Goals / Non-Goals

**Goals:**

- One file-hash helper used by every site that hashes a file's contents.
- One implementation of "group inputs by candidate name, then conflict-handle the colliding groups", so the sort key and the group/unique dispatch are stated once.
- Existing planner tests pass **unmodified** — that is the correctness evidence for the extraction.

**Non-Goals:**

- Changing any planned name, digest, or file set.
- Aligning `packer.cpp`'s `scanDirectoryFiles` with `media::scanByExtensions` (behaviour change; see proposal).
- Merging the two `shouldForce*ConflictNaming` predicates (different clauses; see proposal).
- Introducing the per-item record or stage runner — that is `unify-media-item-and-stages`.

## Decisions

### D1: The hash wrappers are deleted, not aliased

Call `core::sha256File` at each site and delete `fileHash` / `fileHashOf` / the inlined block. No local alias is kept: the wrappers exist only because the authors did not find `sha256File`, and a one-line alias would preserve that failure mode.

`sha256File` returns `{}` on `!is_open()` (`src/core/sha256.cpp:155-157`), and **three of the four sites already treat `""` as unreadable** — they need no new branch:

- `src/organize/scan.cpp:46` stores the digest as the item's `contentHash`, where `""` simply never matches anything
- `src/organize/execute.cpp:59` guards on `!existingHash.empty()` before comparing, so `""` means "not identical content"
- `src/tagger/model_store.cpp:183` compares the digest against an expected value, so `""` fails the comparison as it should

**`src/organize/teach.cpp:59` is not one of those three.** Its unreadable path is `continue`, not an `""` hash, so the replacement must keep an explicit guard. The guard is load-bearing rather than cosmetic because `""` is a *reachable* cache key: `analysisMissing` calls `cache.put(items[index].contentHash, outcome)` (`pipeline.cpp:141`) with whatever `scan.cpp:14-19` produced, and `AnalysisCache::put` has no empty-key rejection (`cache.cpp:138-149`). So an item that was readable at scan and unreadable at analysis stores a result under `""`, and an unreadable member during teaching would then match that entry and contribute an unrelated analysis to the folder reference instead of being skipped.

Alternative considered: keep a `fileHash` alias per module for readability — rejected, it re-creates the thing being removed and each call site reads fine as `core::sha256File(path)`. Alternative considered: reject empty keys inside `AnalysisCache::put` — rejected here; that changes cache semantics (a reachable key would become unwritable) and belongs with the cache, not with a dedup pass.

### D2: The planner lives in a new header, `src/core/naming_plan.h`

Not in `collision_naming.h` (D1 context: 11 TUs would inherit `<unordered_map>`/`<vector>` for a helper only 2 of them call) and not in either flow's module (it would then be a cross-module dependency from picture to video or vice versa).

It is header-only — a template with no `.cpp` — so the change adds exactly one file.

Alternative considered: extend `collision_naming.h` — rejected on include-graph cost. This repo has a `reduce-unused-includes` change in its archive; adding a container dependency to a widely-included path-string header runs against that.

### D3: Two name callbacks, because both branches differ

```cpp
// src/core/naming_plan.h
template<class Ty>
auto planNamesByCandidate(
  std::span<fs::path const> inputs,
  std::function<fs::path(fs::path const& input)> const& candidateKey,
  bool forceConflictNaming,
  std::function<Ty(fs::path const& input, fs::path const& candidate)> const& uniqueName,
  std::function<Ty(fs::path const& input, fs::path const& candidate)> const& conflictName
) -> std::unordered_map<fs::path, Ty>;
```

Body: group `inputs` by `candidateKey(input)`; for each `(candidate, group)`, when `group.size() == 1 && !forceConflictNaming` assign `uniqueName(input, candidate)`; otherwise sort the group by `naming::stablePathString` and assign `conflictName(input, candidate)` to every member. Sorting a group of one is a no-op, so the conflict branch's sort matches today's code in both planners.

Call sites (each ~6 lines):

- video — `Ty = fs::path`. The wrapper keeps `planVideoOutputFiles`' existing preconditions before the call: the empty-input early return (`:112`) and the `usesSharedOutputRoot` / `OutputLayout::Keep` error (`:121-124`). Then `candidateKey` = `resolvePlannedOutputDir(...) / EncodeConfig{...}.buildOutputFileName()`; `uniqueName` = `candidate`; `conflictName` = `candidate.parent_path() / naming::buildConflictHandledFlatName(sourceRootDir, input, candidate.stem().string(), candidate.extension().string())`. `ensureUniqueOutputPaths` still runs afterwards, unchanged.
- picture — `Ty = std::string`; the `OutputLayout::Keep` early return (`picture_process.cpp:96-104`) stays before the call; `candidateKey` = `input.filename()`; `uniqueName` = `"1000__" + candidate.generic_string()`; `conflictName` = `"1000__" + naming::buildConflictHandledFlatName(dirPath, input, candidate.stem().string(), candidate.extension().string())`.

`Ty` is a template parameter rather than a fixed type because video's map value is an `fs::path` and picture's is a `std::string` (`PictureEntryPlan`), and a fixed `fs::path` value would force picture to round-trip through `generic_string()`.

Alternative considered: one callback covering both branches — rejected, it changes picture's unique-branch naming (D-context table).

Alternative considered: template the callbacks instead of type-erasing them — rejected as unneeded; the planner runs once per input file, so `std::function`'s indirection is invisible, and `std::function const&` matches `video_encode_runner.h:14`.

### D4: `shouldForce*ConflictNaming` stays duplicated, as a `bool` argument

The helper takes `bool forceConflictNaming`. Each planner keeps its own predicate and passes the result. The video predicate's extra `&& (config.outputFormat != "mp4" || config.packOutput)` clause is behaviour, not duplication, and the helper has no `AppConfig` dependency.

### D5: The hash and naming parts ship as one change

They are independent edits, but both are behaviour-preserving dedup in the same four-module area, and `unify-media-item-and-stages` wants both before it starts. Splitting them would produce two changes with the same review shape and the same verification story.

## Risks / Trade-offs

- **The extraction is not obviously worth it on line count.** The shared body is ~20 lines; each call site keeps ~6 lines of callbacks, so the net saving is roughly 15-20 lines. The value is single-sourcing the grouping + `stablePathString` sort contract, not the line count. If review judges the callbacks cost more than the shared body buys, D3 is the piece to drop and the hash part (D1) stands alone.
- **`fs::path` map keys are compared as `fs::path`.** Both planners already key on `fs::path`; the helper does not change that. The repo's `generic_string()` rule applies to serialization, which neither planner does.
- **Behaviour-preservation rests on unmodified tests.** `tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp` and `tests/naming_strategy_tests.cpp` must pass without edits. If any needs an edit to pass, the extraction changed behaviour and the callback split is wrong — stop and re-derive the table in Context.
- **`std::function` allocation on the call path** → not a risk at this call frequency (once per input file), and no allocation happens for the lambda captures used here beyond the small-buffer limit; accepted.
- **`sha256File` is a behaviour-neutral but not cost-neutral swap.** It streams in 8 KB chunks instead of buffering the whole file, so organize's per-image hashing gets cheaper and its peak memory drops. No output changes.
- **`teach.cpp`'s unreadable path is not the other three's.** Its current behaviour is `continue` while `sha256File` signals unreadable with `""`, which is also a reachable `AnalysisCache` key (D1). → the task keeps the guard and pins it with a case that an unreadable teaching member is skipped; the unmodified `[organize]` cases alone would not catch a dropped guard unless one of them happens to have an unreadable member.

## Migration Plan

1. Replace the four hash wrappers with `core::sha256File`; run the unit suite (organize, tagger, job-state cases cover these paths).
2. Add `src/core/naming_plan.h` with the helper plus one direct test case for its grouping and sort order.
3. Rewrite `planPictureZipEntryNames` (smaller, single layout branch) as a wrapper; run the picture suite.
4. Rewrite `planVideoOutputFiles` as a wrapper; run the video suite.
5. Full `xmake test-report` plus `xmake test-parallel` before commit.

Rollback: each step is independent and revertable; no persistent state or file format is involved.

## Open Questions

None — the two decisions that could have changed the task breakdown (header location, callback split) are settled above.
