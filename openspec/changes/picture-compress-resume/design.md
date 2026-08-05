## Context

The picture compression flow (`--type picture --compress-images`) currently has no resume support: compression is a long batch, and any interruption forces a full recompression; the compression path also carries dead code and duplicates. The video flow's job state is already always on (`shouldEnableJobState`, pipeline.cpp:35). The pack-step plumbing for pictures already exists (buildPicturePackRequest passes `.jobState`; pack::execute → runResumable activates automatically). See proposal.md for motivation.

Design baseline: cache reuse must satisfy two dimensions — **content** (same name, different content: quality) and **identity** (cross-run name collisions: different input dirs, same file names, same output dir). Sub-agent review confirmed all 11 factual claims match the code, and found two HIGH-severity defects (cancel mid-compression wrongly deletes the state; partial temp files could be treated as valid outputs); this design incorporates the fixes.

## Goals / Non-Goals

**Goals:**
- After an interrupted picture compression, a rerun compresses only the remaining pictures; the pack step resumes automatically
- Atomic compression outputs — partial files are never packed
- Cache validity holds strictly under quality changes, replaced sources, and cross-run collisions
- Clean up dead code/duplicates/latent bugs on the compression path

**Non-Goals:**
- Zip output atomicity (hard crash can leave a truncated zip) — known pre-existing limitation, also present in the video path; separate change
- Always-on state for direct picture packing (stays flag-gated)
- Reusing the cache across successful runs (cache is cleared on success; rerun = fresh, same as today)

## Decisions

### D1: Cache validity = quality-keyed directory + store matching, two mechanisms, one dimension each

| Dimension | Mechanism | Reason |
|---|---|---|
| Quality (same name, different content) | Cache dir name encodes quality: `.compress_tmp_q{quality}` (default 2 materialized) | Entry names are a function of config; only quality produces same-name-different-content outputs |
| Identity (cross-run collision) | Reuse the store's ConfigSnapshot matching (9 fields incl. inputPaths already there) | Entry names can collide across runs (same-named files in different dirs); the dir key alone cannot catch this |

- **Alternative (rejected)**: add `imageQuality` to ConfigSnapshot — of the 9 match fields, 8 are inherently safe for the cache (name-keyed paths self-protect); serialization + schema + migration for one field is not worth it.
- **Alternative (rejected)**: marker file with quality inside the cache dir — the dir key is self-describing, no extra file and no write-order conventions.

### D2: Cache reuse gate = `jobStateMatched`

`RuntimeContext` gains `bool jobStateMatched`, set by pipeline::ensureJobState:

```cpp
ctx.runtime.jobStateMatched = initRes.value();  // true = restored from an existing matching state
```

On mismatch, initialize already returns false (discard branch), so `&& !discardedMismatched` is redundant (review LOW-7 simplification). `--restart` → false → cache cleared. **State file deleted but cache leftover** → matched=false → cache cleared (safe default against reusing foreign caches).

### D3: Compression-phase marker task (fixes HIGH-1)

When the compression batch starts, merge a phase marker task into the store (stable id such as `compress-phase`, own kind), then `markRunning`; on cancel `markInterrupted`; on successful completion of the phase `markSucceeded`. Effect: `jobStateNeverStarted` (pipeline.cpp:70, all_of over empty table = true) is false once compression has started — **cancel mid-compression keeps the state**; cancel at the confirmation prompt (no marker task) removes the state, unchanged (existing test pipeline_picture_tests.cpp:79-103 protects this).

- **Alternative (rejected)**: pipeline checks for `.compress_tmp*` directories — leaks the picture cache dir convention into pipeline, and "directory exists" ≠ "compression started".

### D4: Atomic compression outputs (fixes HIGH-2)

`compressImage` writes to `{finalPath}.partial` and renames after ffmpeg exits 0. **"Final file exists" now strictly means "compression completed"** — partial files never satisfy the existence check and are never packed. Leftover `.partial` files are overwritten by `-y` and cleared with the cache directory.

- **Alternative (rejected)**: integrity-checking files on continue — no shared guard, every call site patches itself.

### D5: Resume skip condition = exists && mtime guard (fixes MEDIUM-3)

Skip condition: `output exists && last_write_time(output) >= last_write_time(source)`. Replaced source → newer mtime → recompress → new temp mtime → archive fingerprint (member paths' size+mtime, makeArchiveTask→buildFingerprint) changes → repack. Converges through three layers automatically, aligning with the video side's "fingerprint the source file" semantics.

### D6: Pack-planning decision becomes a pure function (B5)

`usedCompressed` moves from compress time (stored in memory in CompressResult) to pack planning, decided per picture from the filesystem:

```
output exists && output <= source  → pack output with .jpg entry name
output exists && output > source   → pack original with original entry name (output kept as "done" marker)
no output                          → skip entry (compression failed, not packed — preserves current semantics)
source missing                     → skip entry
```

The output path is a deterministic pure function `cacheDir / toJpgEntryName(plannedEntryName)`; the `buildCompressedResultLookup`/`buildCompressTaskKey` lookup table is deleted. `CompressResult` is slimmed down (drop `usedCompressed`); `compressImageBatch`'s return value only serves counting/summary and the "all failed" check.

### D7: Cache lifecycle table

| Event | Cache | State |
|---|---|---|
| Start, matched && key dir exists | Keep key dir, clear other `.compress_tmp*`; skip compressed | Resume |
| Start, otherwise | Clear all `.compress_tmp*`, rebuild | Fresh |
| Cancel (after compression started) | Keep | Keep (marker task Interrupted) |
| Cancel at confirmation prompt | — | Remove (existing behavior) |
| Success | Remove | completed |
| Fatal error (pack failure / all compressions failed) | Remove | Keep (for investigation) |
| `--restart` | Clear all | Rebuild |

Note: the unconditional `remove_all(tempDir)` at picture_process.cpp:488 must be reordered after the cancel check and made conditional per the table (review LOW-8).

## Risks / Trade-offs

- **[FAT filesystem mtime granularity]** → primary platform NTFS has high resolution (100ns); if FAT support is ever needed, add a size check. Windows primary; accepted.
- **[Rerun after success = full recompression]** → consistent with today; the cache serves interruptions only. Future evolution: keep the cache and reuse it; not in this change.
- **[Transient cache disk usage]** → oversized outputs linger after cancel (original-file order of magnitude); cleared by the next successful resume run. Accepted.
- **[State file written on every picture-compression run]** → the price of always-on, same as video.
- **[Truncated zip issue (pre-existing)]** → in Non-Goals; does not affect this change's compression-side guarantees.

## Migration Plan

No data migration. Legacy `.compress_tmp` (unkeyed) directories are covered by the startup glob cleanup. Rollback: revert the commit; leftover `.compress_tmp_q*` directories are cleared by the glob on the next run (any quality), no residue.

## Open Questions

- Whether a recovery message for the compression phase (mirroring video's "Recovered N completed task(s)") is worth adding — deferrable; does not affect specs, design, or tasks.
