# Design

## Context

See `proposal.md` — Why. The constraints that shape the approach come from what already exists in the tree:

- `jobstate::detail::flushSnapshot` (`src/core/job_state.cpp`) stages the snapshot to `<path>.tmp`, `fs::rename`s it over the state file, and — when that rename fails — removes the state file and retries. `std::filesystem::rename` on Windows already replaces an existing target (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`), so the retry only runs for a locked target; the retry is also the only path that can leave the state path empty.
- `ConfigSnapshot` (`src/core/job_state.h`) enumerates the fields a resume compares, and `configMatches` already treats one field leniently (`packOutput`) plus one legacy-compatible field (`packOutputDir`, absent in older states).
- `collisionnaming::stablePathString` case-folds, and three call-site families decide identity from it: job-state task ids (`makeEncodeTask`), pack grouping keys (`buildPackEntryStableKey`), and `shortPathHash` (conflict-handled file names and segment-directory names). `planVideoOutputFiles` dedupes through `path_map` (`std::unordered_map<fs::path>`), which is exact-case on both platforms — nothing to change there.
- `media::scanByExtensions` (`src/core/media_scanner.cpp`) is the one directory walk that already handles errors correctly (`skip_permission_denied`, `error_code` increment, warning per failure); five other walks do not (packer, organize disposition pre-pass, organize reference sampling, batch subfolder discovery, picture cache sweep, tool search).
- The probe cache (`src/video/probe_cache.cpp`) already treats unparseable JSON and an unknown version as an empty cache; only the missing-`entries` case escapes that policy.
- The tagger's presence check and its download loop read two different predicates (`missingFiles`, which compares size, and `fs::exists`).

## Goals / Non-Goals

**Goals:**

- Each of the fourteen findings gets one behaviour change, one test that fails before it, and a place in a spec.
- No new dependency, no on-disk schema bump, no new module.
- Every fix is the smallest one that removes the defect; where two helpers do one job, the fix deletes one.

**Non-Goals:**

- Re-tuning the probe or segment logic (the audit found it sound).
- Widening the media whitelists beyond the pack whitelist's video entries plus `.m2ts`.
- Changing how batches split work, or how progress is rendered.
- Adding fault-injection infrastructure.

## Decisions

### D1 — Settings ride in the snapshot, and an absent field matches

`ConfigSnapshot` gains one `std::optional<EncodeSettings>` holding `crf`, `videoCodec`, `nvencPreset` and `minVmaf`. The nested optional is what makes "recorded" distinguishable from "recorded as unset": a run without `--crf` lets the probe pick the quality, which is a different job from `--crf 28`, so the fields themselves stay optional and the parent's presence carries the record. `toJson`/`fromJsonConfig` write and read it; `configMatches` requires equality **when the parent is present**, and treats an absent parent (a state written by an earlier build) as matching. Alternatives considered: (a) bump `kStateVersion` so old states are rejected outright — rejected, it discards in-progress work on upgrade for a defect that only bites when settings actually differ; (b) fold the settings into the task fingerprint — rejected, the fingerprint is per task and the settings are per run, so it would rewrite every task's identity for one run-level change.

`videoCodec` is included even though it already changes the output file name (and therefore the task fingerprint): the snapshot is the run's settings record, and leaving one setting out makes the requirement false in exactly the case a future naming change would make real. `imageQuality` is **not** included: it does not change a video output's bytes, and the existing contract that a picture quality change leaves the video-to-WebP conversion cache reusable (`tests/picture/picture_video_webp_tests.cpp`) must survive, while picture compression is already keyed by quality in its cache directory name.

### D2 — The commit never empties the state path, and the rule is testable without a hook

The replace moves into `jobstate::detail::replaceStateFile(stagedPath, statePath)`, which renames the state file to `<path>.bak` (tolerating "no current file"), renames the staged file over the state path, restores `.bak` when that fails, and deletes `.bak` only after success. The remove-then-retry step disappears, so no path empties the state path.

The invariant is tested by calling the helper directly, not by observing a window from outside: with a staged path that does not exist the helper must fail **and leave the state file untouched**, which the current remove-first retry violates. Alternatives: (a) `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` — Windows-only, so POSIX keeps the window; (b) a pre-commit test hook inside `flushSnapshot` — rejected: it only observes today's happy path (where the state file is present when the rename runs), so the test would be green before the fix and would pin nothing; (c) leaving the retry and documenting it — rejected, the helper costs the same and removes the failure mode.

### D3 — Identity stops case-folding; ordering keeps folding

New `collisionnaming::pathIdentityString(path)`: exact case on POSIX, case-folded on Windows (`#if defined(_WIN32)`, the same compile-time split the module's callers already use for platform behaviour). Identity call sites move to it: `makeEncodeTask`'s task id, `buildPackEntryStableKey`, and `shortPathHash` (which feeds conflict-handled file names and `segmentDirForTask`). `stablePathString` stays as the ordering key (`stablePathLess`), because ordering only needs one stable total order and the existing tests pin that order.

Alternatives: (a) runtime-probe the filesystem's case sensitivity — rejected, it needs a probe file per run and answers the same thing on both supported platforms; (b) keep folding everywhere and reject ambiguous batches — rejected, it turns a silent wrong result into a user-visible error for a case that is legal on Linux.

On Windows nothing changes (the identity string is still folded). On POSIX, conflict-handled file names and segment-directory names change once; an in-flight POSIX job re-encodes from scratch and leaves its old segment directory under `.encro/segments` orphaned until the output tree is cleaned. Accepted: it is a one-time cost on a platform where the previous behaviour was wrong.

### D4 — The probe cache follows its own documented policy

`probecache::load` replaces `obj.at("entries")` with a `contains` + `is_array` check and returns the empty cache, which is what the parse-error and version paths already return. No schema change, no new field.

### D5 — One predicate drives presence and download; the presence-checked DLL is renamed last

`downloadMissing` iterates the `missingFiles()` result instead of `allFilesPresent()` plus a per-file `fs::exists`. The pinned file list stays the single source of truth (the spec already requires that); the fix only makes the download loop read it.

The cuDNN install stages each DLL as `<name>.part`, checks the stream state after writing, then renames every staged file in a second pass — renaming `cudnn64_9.dll` **last**, because that is the name the presence check tests. An interrupted extraction therefore cannot make the runtime look installed, and no separate marker file is needed. Alternatives: a marker file — rejected, one more thing to keep in sync; verifying each DLL's checksum — rejected, NVIDIA publishes no per-file digests and the archive's checksum already covers the bytes.

### D6 — One walker owns directory-error tolerance

`media::listFiles(root, recursive)` is added next to `scanByExtensions` and holds the guarded loop (skip_permission_denied, `increment(ec)`, one warning per failure). The packer's `scanDirectoryFiles` is replaced by it plus the `.encro` filter and the excluded-path check, and the remaining flagged walks adopt the same idiom where their shape allows it: the organize disposition pre-pass, organize reference sampling, batch subfolder discovery, the picture cache sweep and the ffmpeg/ffprobe search. Walks that need per-entry type information keep their own loop but take the `error_code` form. `groupFilesBySize`/`groupPackEntries` read sizes through the `error_code` overload and treat a vanished file as unreadable rather than throwing.

Alternatives: one generic visitor for every walk — rejected; the consumers need different per-entry answers (extension match, `is_directory()`, file name match), and a visitor parameter would be more machinery than the guarded loops it replaces.

`findFFprobe`/`findFFmpeg` collapse into one `findTool(name, installDir)` with two thin wrappers, which is also where the guarded loop lives once.

### D7 — The video container set is a superset of the pack whitelist's video entries

`kVideoTypes` gains `.webm`, `.m4v`, `.ts`, `.mpg`, `.mpeg`, `.3gp` — the pack media whitelist's video entries, which the video scan was missing — plus `.m2ts`, which the whitelist does not list but which is the common AVCHD spelling of the same container. The WebP input size gate and the "already HEVC" filter are unchanged. Alternative: bypass the extension filter for an explicitly named file — rejected, it would accept `.txt` as a video input and push the misdetection to ffprobe, and the whitelist would still be wrong for directory scans.

### D8 — The packer's walk excludes `.encro` by name, not by dot-prefix

The packer skips any directory entry named `.encro` at any depth. Dot-prefixed files and other dot-prefixed directories stay eligible, so packing a user tree does not quietly drop `.config` or `.gitignore`. Alternative: reuse the scanner's "skip every dot directory" rule — rejected, it changes what users get from a pack of their own tree, which the fix does not need.

### D9 — A prompt answer is two-valued plus "no answer"

`utils::readUserIpt` returns `std::optional<bool>`: `nullopt` means no answer (stdin at end-of-file, or a stop request landing during the prompt). `readUserIptOnce` keeps its `std::optional<bool>` shared slot and stores only `true`/`false`. All three call sites (`video_process.cpp`, `video_batch_execution.cpp`, `picture_process.cpp`) map `nullopt` to the existing cancellation path while a stop is pending, and to an error message naming `--yes` with exit code 1 otherwise. Alternatives: (a) keep `bool` and set a flag on `AppContext` — rejected, action at a distance for a value the caller already holds; (b) a fourth state distinguishing "stop" from "no stdin" — rejected, `isStopRequested()` already answers that and every caller checks it.

### D10 — The already-HEVC count comes back from the filter, and the caller narrates it

`finalizeVideoList` returns the surviving paths plus the number it dropped; `readAllVids` prints the single hint line when the scan root was a directory. Single-file inputs keep today's message in `printNoEncodableVideosMessage`, so the two paths stay distinct and no line prints twice.

### D11 — Dry run counts decisions, not copies

`organize::executeOrganize` always resolves the destination through the same identity check; in a dry run the outcome (would-copy vs already-there) is counted into `copied` / `skippedExisting`, and `ReportData` gains `dryRun` so the totals line reads `would copy N` instead of claiming copies that did not happen. The construction site in `organize/pipeline.cpp` fills the new field. Alternative: a separate `planned` field — rejected, the two outcomes are exactly the two numbers the report already has.

## Risks / Trade-offs

- [Fourteen fixes in one change is a big diff] → the tasks are split per finding with one commit each, so a revert is per finding and the specs pin each behaviour independently.
- [POSIX names and hashes change once (D3)] → stated in the proposal's impact; the orphaned segment directory is removed with the output tree, and Windows is byte-identical to today.
- [A legacy state matches any setting value (D1)] → deliberate: the bug survives exactly one upgrade boundary, and the alternative loses in-progress work. The new scenario pins the leniency so it cannot silently become strictness later.
- [The committed replace can fail where the old retry would have succeeded (a locked target)] → the failure is reported at error level and the previous snapshot survives; a retry costs one run, a lost state costs the run.
- [Excluding `.encro` from packing (D8) could surprise someone who deliberately archived a work root] → they can pack the directory with `--output` pointing elsewhere, or pack the files directly; the alternative (dot-prefix rule) surprises more users.
- [`.webm`/`.ts` becoming video inputs (D7) means more files are encoded by an existing command line] → intended: those files were being skipped silently, which is the defect.
- [The prompt's `nullopt` reaches three call sites, not one] → each maps it through the same two-clause rule, and the picture pack prompt is covered by its own case.

## Migration Plan

No format or CLI migration. Deploy as one change; the only upgrade effects are the two named above (a state written before the settings were recorded still resumes; POSIX path-derived names change once). Rollback is a per-finding revert — the deltas were written so each requirement can be removed without touching the others.

## Open Questions

None.
