## Context

See `proposal.md` — Why for the four hash wrappers and the two naming planners.

Constraints that shaped the design:

- `core::collision_naming.h` is included by ten translation units under `src/` (nine directly; `video_encode_runner.cpp` through `segment_dir.h`) and by three test files, and currently carries only light headers (`<algorithm>`, `<cctype>`, `<cstdint>`, `<filesystem>`, `<format>`, `<optional>`, `<string>`, `<string_view>`). A container-based planner added there would put `<unordered_map>` and `<vector>` into all of them.
- House style for callbacks is `std::function<...> const&` — `src/video/video_encode_runner.h:14` defines `using function_ref = std::function<void(std::string const&)> const&;`. `std::function_ref` is not used anywhere in the repo. This mattered only to the reverted helper (`D3`); nothing in the shipped change takes a callback.
- Project language level is C++26 (`xmake.lua:5`), toolchain `clang-cl` on Windows.

The two planners are **not** the same function with one varying callback. They differ in *both* branches:

| | unique branch (`size()==1 && !force`) | conflict branch |
| --- | --- | --- |
| video (`video_output_planning.cpp:142`) | raw `candidatePath` | `candidate.parent_path() / buildConflictHandledFlatName(sourceRootDir, input, stem, ext)` (`:28-39`) |
| picture (`picture_process.cpp:113-116`) | `"1000__" + fileName` | `"1000__" + buildConflictHandledFlatName(dirPath, input, stem, ext)` (`:66-76`) |

Picture wraps the unique case too; video does not. A single "name builder" callback would silently change one of the two.

## Goals / Non-Goals

**Goals:**

- One file-hash helper used by every site that hashes a file's contents.
- One implementation of the case-folded path ordering every conflict-naming site sorts by, so that rule is stated once. The broader goal — one implementation of the whole "group inputs by candidate name, then conflict-handle the colliding groups" loop — was measured and reverted; see `D3`.
- Existing planner, picture and job-state tests pass **unmodified** — that is the correctness evidence for both swaps.

**Non-Goals:**

- Changing any planned name, digest, or file set.
- Aligning `packer.cpp`'s `scanDirectoryFiles` with `media::scanByExtensions` (behaviour change; see proposal).
- Merging the two `shouldForce*ConflictNaming` predicates (different clauses; see proposal).
- Introducing the per-item record or stage runner — that is `unify-media-item-and-stages`.

## Decisions

### D1: The hash wrappers are deleted, not aliased

Call `core::sha256File` at each site and delete `fileHash` / `fileHashOf` / the inlined block. No local alias is kept: the wrappers exist only because the authors did not find `sha256File`, and a one-line alias would preserve that failure mode.

`sha256File` returns `{}` on `!is_open()` (`src/core/sha256.cpp:155-157`), and **three of the four sites already treat `""` as unreadable** — they need no new branch:

- `src/organize/scan.cpp:37` stores the digest as the item's `contentHash`, where `""` simply never matches anything
- `src/organize/execute.cpp:46` guards on `!existingHash.empty()` before comparing, so `""` means "not identical content"
- `src/tagger/model_store.cpp:176` compares the digest against an expected value, so `""` fails the comparison as it should

**`src/organize/teach.cpp:60` is not one of those three.** Its unreadable path is `continue`, not an `""` hash, so the replacement must keep an explicit guard. The guard is load-bearing rather than cosmetic because `""` is a *reachable* cache key: `analyzeMissing` calls `cache.put(items[index].contentHash, outcome)` (`src/organize/pipeline.cpp:141`) with whatever `scan.cpp` produced, and `AnalysisCache::put` has no empty-key rejection (`cache.cpp:138-149`). So an item that was readable at scan and unreadable at analysis stores a result under `""`, and an unreadable member during teaching would then match that entry and contribute an unrelated analysis to the folder reference instead of being skipped.

Alternative considered: keep a `fileHash` alias per module for readability — rejected, it re-creates the thing being removed and each call site reads fine as `core::sha256File(path)`. Alternative considered: reject empty keys inside `AnalysisCache::put` — rejected here; that changes cache semantics (a reachable key would become unwritable) and belongs with the cache, not with a dedup pass.

### D2: The comparator lives in `collision_naming.h`; the planner header did not survive review

A first pass added `src/core/naming_plan.h` — a header-only template holding the whole grouped-candidate loop — deliberately kept out of `collision_naming.h` so the ten translation units that include the latter would not inherit `<unordered_map>`/`<vector>` for a helper only two of them call.

The Post-Change Review rejected that header (see `D3`). What remains is the rule that was genuinely shared and worth stating once: the case-folded ordering. `collisionnaming::stablePathLess` sits in `collision_naming.h` beside `stablePathString`, which it calls, and needs no include the header lacks — a three-line function needs no container support. It serves four sites, not two (see Impact in proposal.md).

### D3: The planner is not extracted — measured, then reverted

The first pass moved the grouped-candidate loop into `planNamesByCandidate`, a `Ty`-templated function taking a candidate key plus two name callbacks, on the reasoning that the two planners are the same algorithm over the same data shape.

The review measured it rather than arguing about it, from `git diff --numstat`:

| | lines |
| --- | --- |
| `naming_plan.h` | +57 |
| `video_output_planning.cpp` call site | +14 / −34 |
| `picture_process.cpp` call site | +13 / −26 |
| **source net** | **+24** |
| `tests/naming_plan_tests.cpp` | +94 |

So it added 118 lines to fold away 33 lines of duplicated dispatch at two call sites. This design's own Risks section had pre-authorised exactly this outcome — "if review judges the callbacks cost more than the shared body buys, D3 is the piece to drop and the hash part (D1) stands alone" — and the prediction the extraction rested on ("the net saving is roughly 15-20 lines") was wrong: the callers keep ~13 lines of callbacks each, not the ~6 assumed.

Two further facts, both established by an independent verification agent, shaped what replaced it:

- The candidate keys differ in **type**, not just value: video groups on the full `outputDir / fileName` candidate as an `fs::path`, picture on `filePath.filename().generic_string()` as a `std::string`. That is why the helper needed a `Ty` template parameter at all — a symptom of the two flows agreeing less than the extraction assumed.
- The sort inside the loop is **not** dead weight, contrary to the review's leanness axis. Both original planners sort a colliding group before inserting into the returned map, and that map is a `std::unordered_map` whose `begin()` `summaryOutputDir` reads (`src/video/video_process.cpp:281`); with separate chaining, same-bucket keys iterate in reverse insertion order, so insertion order can change which directory gets reported. Dropping the sort would have been a behaviour change, not a simplification. The latent `begin()` defect itself is recorded in `docs/backlog.md` rather than fixed here.

What replaced it is `stablePathLess` (`D2`): three lines, four call sites, no callbacks and no template parameter.

Alternative considered: keep the planner but template the callback parameters instead of type-erasing them — rejected, it removes ~5 lines and neither the +24 source nor the +94 test cost.

### D4: `shouldForce*ConflictNaming` stays duplicated

The two predicates are not merged — each planner keeps its own. The video predicate's extra `&& (config.outputFormat != "mp4" || config.packOutput)` clause is behaviour, not duplication, and sharing anything here would force an `AppConfig` dependency into a path-string header. The `bool forceConflictNaming` parameter this decision used to describe went with the reverted helper (`D3`).

### D5: The hash dedup and the comparator ship as one change

They are independent edits, but both are behaviour-preserving dedup in the same area, and splitting them would produce two changes with the same review shape and the same verification story. The naming *planner* was originally part of this change too; `D3` records why it is not.

## Risks / Trade-offs

- **The extraction did not repay its cost, and the design's own fallback fired.** This section predicted a 15-20 line net saving; measurement after implementation showed **+24 source and +94 test lines**, because each call site keeps ~13 lines of callbacks rather than the ~6 assumed. `D3` records the revert and what replaced it. The lesson worth keeping: predict a refactor's line cost from the *measured* call sites, not from the shape of the shared body.
- **The two planners group on different key *types*.** Video's candidate is an `fs::path` (`outputDir / fileName`); picture's is a filename `std::string`. Any future attempt to share this loop has to reckon with that, and it was one of the signals that the two flows agree less than their shape suggests. The repo's `generic_string()` rule applies to serialization, which neither planner does.
- **Behaviour-preservation rests on unmodified tests.** `tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp` and `tests/naming_strategy_tests.cpp` must pass without edits. If any needs an edit to pass, the comparator swap changed behaviour — stop and re-derive the table in Context (and by extension the `stablePathLess` extraction).
- **`std::function` allocation on the call path** → this risk retired with the reverted helper (`D3`): no callbacks are left, so nothing is type-erased.
- **`sha256File` is a behaviour-neutral but not cost-neutral swap.** It streams in 8 KB chunks instead of buffering the whole file, so organize's per-image hashing gets cheaper and its peak memory drops. No output changes.
- **`teach.cpp`'s unreadable path is not the other three's.** Its current behaviour is `continue` while `sha256File` signals unreadable with `""`, which is also a reachable `AnalysisCache` key (D1). → the task keeps the guard and pins it with a case that an unreadable teaching member is skipped; the unmodified `[organize]` cases alone would not catch a dropped guard unless one of them happens to have an unreadable member.

## Migration Plan

1. Replace the four hash wrappers with `core::sha256File`; run the unit suite (organize, tagger, job-state cases cover these paths).
2. Add `src/core/naming_plan.h` with a grouped-candidate planner plus a case for its grouping and sort order, and rewrite both planners as wrappers. **This step was reverted** — the Post-Change Review measured it at +24 source and +94 test lines (`D3`), the header and its test are gone, and the planners are back to their original loops.
3. Add `collisionnaming::stablePathLess` and point the four sort sites at it; run the plan-output, picture-process and job-state tags.
4. Full `xmake test-report`, `xmake test-parallel` and the e2e suite before commit.

Rollback: each step is independent and revertable; no persistent state or file format is involved. Step 2 was in fact rolled back, which is the evidence that the separation holds.

## Open Questions

None. The two decisions that could have changed the task breakdown (header location, callback split) were settled, and then reversed by measurement in `D3`, so nothing is deferred here.
