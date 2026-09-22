## Why

`core::sha256File` (`src/core/sha256.h:15`) already hashes a file by streaming it in 8 KB chunks and returning `""` when the file cannot be opened. Four modules do not use it and instead buffer **the whole file** into a `std::string` and hand the bytes to `core::sha256Hex` — a byte-identical five-line wrapper written four times: `src/organize/scan.cpp:14-19` (`fileHash`), `src/organize/execute.cpp:37-42` (`fileHashOf`), `src/organize/teach.cpp:57-61` (inlined), `src/tagger/model_store.cpp:39-44` (`fileHash`). The unreadable-file contract already matches exactly (`sha256File` returns `{}` on `!is_open()`, `src/core/sha256.cpp:155-157`), so the only two differences are the duplication and the unbounded allocation: organize hashes every scanned image this way (`scan.cpp:14`), and `execute.cpp:37` re-hashes destination files during collision resolution. (Those wrapper locations are the pre-change tree; this change deleted them.)

The naming planner is duplicated the same way. `planVideoOutputFiles` (`src/video/video_output_planning.cpp:104-158`) and `planPictureZipEntryNames` (`src/picture/picture_process.cpp:88-130`) are the same algorithm over the same data shape — build `candidate -> [inputs]`, then for each candidate either take it as-is (single input, conflict naming off) or sort the group by `naming::stablePathString` and route every member through `naming::buildConflictHandledFlatName` — differing only in how the candidate key is derived (video: `resolvePlannedOutputDir` + `EncodeConfig::buildOutputFileName`, `video_output_planning.cpp:134-138`; picture: `filePath.filename()`, `picture_process.cpp:110`) and in what wraps the conflict-handled name (video: `parent_path() / name`, `video_output_planning.cpp:28-41`; picture: `"1000__" + name`, `picture_process.cpp:48-50`). About 50 lines, twice.

Why now: `unify-media-item-and-stages` puts a per-item record in all four flows and rebuilds each flow's plan stage on top of these two functions. Sharing the ordering rule now means that change cannot copy four slightly different sort comparators into the new seam — which is what this half of the change was reduced to after review (see design.md `D3`).

## What Changes

- **Four file-hash wrappers deleted** in favour of `core::sha256File`: `src/organize/scan.cpp:14-19`, `src/organize/execute.cpp:37-42`, `src/organize/teach.cpp:57-61`, `src/tagger/model_store.cpp:39-44`. Same digest, same `""`-on-unreadable result; the side effect is that a file is no longer buffered in full just to be hashed.
- **One shared path comparator, not a shared planner.** A first pass extracted the whole grouped-candidate loop behind two name callbacks; the Post-Change Review rejected that on measurement (+24 source lines and +94 test lines to fold away ~33 lines of duplicated dispatch at two call sites), so the design's `D3` fallback was taken and the extraction is reverted. What ships instead is the one rule that was genuinely shared and worth stating once: the case-folded `stablePathString` ordering becomes `collisionnaming::stablePathLess`, called by the four sites that sort paths that way. See design.md `D2`/`D3`.
- **Not merged**: `shouldForceConflictNaming` (`video_output_planning.cpp:22-26`) and `shouldForcePictureConflictNaming` (`picture_process.cpp:61-64`). The video predicate carries an extra `&& (config.outputFormat != "mp4" || config.packOutput)` clause; folding them together would change which video runs get conflict suffixes. The flag stays a `bool` parameter.
- **Not merged**: `ensureUniqueOutputPaths` (`video_output_planning.cpp:64-88`) stays video-only. It is a second-pass uniqueness guard over already-planned paths and has no picture counterpart; adding one to picture would be a behaviour change, not a dedup.
- **Not merged**: pack's `scanDirectoryFiles` (`src/pack/packer.cpp:677-708`) stays as it is. It deliberately does not skip dot entries and does not use `fs::directory_options::skip_permission_denied`, and it collects no warnings, while `media::scanByExtensions` does all three (`src/core/media_scanner.cpp:16`, `:29-33`, `:41`, `:49`). Pointing pack at the shared scanner changes the file set and adds a warnings surface — a behaviour change that belongs in its own change.
- **Not merged**: `src/tagger/engine_factory.cpp:51-54` keeps its own read-and-hash block. It is the same shape as the four wrappers but returns `eh::makeError("cannot read {}", path)` when the file will not open (`:52`), while `sha256File` returns `""` (`src/core/sha256.cpp:155-157`). Swapping it would turn a reported error into a lookup miss, so it is a behaviour change and stays out.
- **Tests**: existing coverage for both planners (`tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp`, `tests/naming_strategy_tests.cpp`) must stay green **unmodified** — that is the evidence the hash swap and the comparator swap are behaviour-preserving. No new case is owed for `stablePathLess`: it is a three-line expression the four call sites already exercise, and the 94-line harness written for the rejected planner went with it.

No behaviour change in any command: the same names are planned, the same digests are computed, the same files are hashed.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is a pure internal deduplication with no requirement changes. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-task-outcome`, `reduce-over-engineering` and `refactor-long-param-lists`.

## Impact

- Deleted: four local hash wrappers in `src/organize/scan.cpp`, `src/organize/execute.cpp`, `src/organize/teach.cpp`, `src/tagger/model_store.cpp`
- New: `collisionnaming::stablePathLess` in `src/core/collision_naming.h`, one function
- Reverted: `planVideoOutputFiles` (`src/video/video_output_planning.cpp`) and `planPictureZipEntryNames` (`src/picture/picture_process.cpp`) keep their original inline grouping; only their sort now calls the shared comparator, as do `collectFolderSummaryPictures` (`picture_process.cpp:188`) and the fingerprint input ordering (`src/core/job_state.cpp:542`)
- Tests: existing cases stay green unmodified — the evidence both swaps preserved behaviour — plus one new `[organize]` case pinning that teaching skips a member it cannot read (task 1.3); the naming half owes no test, and the 94-line harness written for the reverted planner went with it
- No CLI, file-format, job-state or resume surface touched; no new dependency

**Explicitly out of scope:** the per-item record and stage-runner unification (`unify-media-item-and-stages`) and the video migration (`migrate-video-to-media-items`), aligning pack's directory scan with `media::scanByExtensions`, and `src/tagger/engine_factory.cpp:51-54` (different unreadable-file contract).
