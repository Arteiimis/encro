## Why

`core::sha256File` (`src/core/sha256.h:15`) already hashes a file by streaming it in 8 KB chunks and returning `""` when the file cannot be opened. Four modules do not use it and instead buffer **the whole file** into a `std::string` and hand the bytes to `core::sha256Hex` — a byte-identical five-line wrapper written four times: `src/organize/scan.cpp:14-19` (`fileHash`), `src/organize/execute.cpp:37-42` (`fileHashOf`), `src/organize/teach.cpp:57-61` (inlined), `src/tagger/model_store.cpp:39-44` (`fileHash`). The unreadable-file contract already matches exactly (`sha256File` returns `{}` on `!is_open()`, `src/core/sha256.cpp:155-157`), so the only two differences are the duplication and the unbounded allocation: organize hashes every scanned image this way (`scan.cpp:14`), and `execute.cpp:37` re-hashes destination files during collision resolution.

The naming planner is duplicated the same way. `planVideoOutputFiles` (`src/video/video_output_planning.cpp:104-161`) and `planPictureZipEntryNames` (`src/picture/picture_process.cpp:88-136`) are the same algorithm over the same data shape — build `candidate -> [inputs]`, then for each candidate either take it as-is (single input, conflict naming off) or sort the group by `naming::stablePathString` and route every member through `naming::buildConflictHandledFlatName` — differing only in how the candidate key is derived (video: `resolvePlannedOutputDir` + `EncodeConfig::buildOutputFileName`, `video_output_planning.cpp:134-138`; picture: `filePath.filename()`, `picture_process.cpp:106`) and in what wraps the conflict-handled name (video: `parent_path() / name`, `video_output_planning.cpp:28-39`; picture: `"1000__" + name`, `picture_process.cpp:48-50`). About 50 lines, twice.

Why now: `unify-media-item-and-stages` puts a per-item record in all four flows and rebuilds each flow's plan stage on top of these two functions. Deduplicating them first stops that change from copying the duplication into the new seam.

## What Changes

- **Four file-hash wrappers deleted** in favour of `core::sha256File`: `src/organize/scan.cpp:14-19`, `src/organize/execute.cpp:37-42`, `src/organize/teach.cpp:57-61`, `src/tagger/model_store.cpp:39-44`. Same digest, same `""`-on-unreadable result; the side effect is that a file is no longer buffered in full just to be hashed.
- **One grouped-candidate naming planner** extracted behind two callbacks: `candidateKey(input) -> fs::path` and `finalName(input, candidate) -> fs::path`. Both existing planners become thin wrappers supplying their own key and name wrapper, so their output is unchanged name for name.
- **Not merged**: `shouldForceConflictNaming` (`video_output_planning.cpp:22-26`) and `shouldForcePictureConflictNaming` (`picture_process.cpp:61-64`). The video predicate carries an extra `&& (config.outputFormat != "mp4" || config.packOutput)` clause; folding them together would change which video runs get conflict suffixes. The flag stays a `bool` parameter.
- **Not merged**: `ensureUniqueOutputPaths` (`video_output_planning.cpp:64-88`) stays video-only. It is a second-pass uniqueness guard over already-planned paths and has no picture counterpart; adding one to picture would be a behaviour change, not a dedup.
- **Not merged**: pack's `scanDirectoryFiles` (`src/pack/packer.cpp:677-708`) stays as it is. It deliberately does not skip dot entries and does not use `fs::directory_options::skip_permission_denied`, and it collects no warnings, while `media::scanByExtensions` does all three (`src/core/media_scanner.cpp:16`, `:29-33`, `:41`, `:49`). Pointing pack at the shared scanner changes the file set and adds a warnings surface — a behaviour change that belongs in its own change.
- **Not merged**: `src/tagger/engine_factory.cpp:51-54` keeps its own read-and-hash block. It is the same shape as the four wrappers but returns `eh::makeError("cannot read {}", path)` when the file will not open (`:52`), while `sha256File` returns `""` (`src/core/sha256.cpp:155-157`). Swapping it would turn a reported error into a lookup miss, so it is a behaviour change and stays out.
- **Tests**: existing coverage for both planners (`tests/video/video_output_planning_tests.cpp`, `tests/picture/picture_process_tests.cpp`, `tests/naming_strategy_tests.cpp`) must stay green **unmodified** — that is the evidence the extraction is behaviour-preserving. One new case pins the shared helper's grouping and sort order directly.

No behaviour change in any command: the same names are planned, the same digests are computed, the same files are hashed.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None — this is a pure internal deduplication with no requirement changes. `skip_specs: true` is set in `.openspec.yaml` per the precedent of `unify-task-outcome`, `reduce-over-engineering` and `refactor-long-param-lists`.

## Impact

- Deleted: four local hash wrappers in `src/organize/scan.cpp`, `src/organize/execute.cpp`, `src/organize/teach.cpp`, `src/tagger/model_store.cpp`
- New: one shared naming-planner function (candidate home decided in design.md)
- Rewritten: `planVideoOutputFiles` (`src/video/video_output_planning.cpp:104-161`) and `planPictureZipEntryNames` (`src/picture/picture_process.cpp:88-136`) become wrappers over the shared helper
- Tests: no edits expected to existing cases; one or two new cases for the extracted helper
- No CLI, file-format, job-state or resume surface touched; no new dependency

**Explicitly out of scope:** the per-item record and stage-runner unification (`unify-media-item-and-stages`) and the video migration (`migrate-video-to-media-items`), aligning pack's directory scan with `media::scanByExtensions`, and `src/tagger/engine_factory.cpp:51-54` (different unreadable-file contract).
