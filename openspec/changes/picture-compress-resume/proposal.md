## Why

The `--type picture --compress-images` flow does not support resuming: compression is a long batch (thousands of images, minutes of work), and any interruption (cancel, power loss, crash) forces recompressing every picture, while the video flow already has job state always on. The compression path also carries dead code and verbatim duplicates (confirmed accurate by sub-agent review); this change cleans those up too.

## What Changes

**Picture compression resume (B1-B7):**
- `--type picture --compress-images` enables job state by default (aligned with video); direct picture packing (no compression) stays flag-gated (`--resume`/`--restart`/`--state-file`)
- Compression cache directory is keyed by quality: `.compress_tmp_q{quality}` (default 2 materialized); cache reuse requires a matching saved state (`jobStateMatched`) and the current key directory existing
- Cancel keeps the cache and state (a later run resumes by default); success clears the cache; config mismatch or `--restart` clears all `.compress_tmp*`
- Resume skip condition: compressed output exists and its mtime >= source image mtime (replaced source files recompress automatically, converging through three layers: recompress → new mtime → archive fingerprint change → repack)
- Atomic compression outputs: ffmpeg writes `{path}.partial`, renamed after exit code 0 — "output exists" strictly means "compression completed", partial files are never packed
- The compression phase records a phase marker task in the store, marked Interrupted on cancel — `maybeRemoveUnstartedCanceledJobState` no longer deletes the state mid-compression (cancel at the confirmation prompt still removes it, unchanged)
- `usedCompressed` decision moves from compress time to pack-planning time (re-derived from the filesystem); `CompressResult` is slimmed down
- Pack step resuming is zero-change (plumbing already exists; it activates once the store is always on)

**Cleanup (A1-A4):**
- Delete `packAllPicsToZip` (no callers)
- Fold the two verbatim copies of `canceledExitCodeForPromptAbort` into `stopsignal`
- Extract a shared helper for the compression-result finalization logic in picture_compress.cpp (compress → compare sizes → delete oversized → record result)
- Add an `isSummary` guard to `applyEntryNameOverrides` (latent bug, zero behavior change today)

**Explicitly out of scope:** zip output atomicity (a hard crash can leave a truncated zip treated as complete) — known pre-existing limitation, also affecting the video path; separate change.

## Capabilities

### New Capabilities
- `picture-compress-resume`: resume support for the picture compression flow — cache directory lifecycle, quality key, atomic compression outputs, resume skip semantics

### Modified Capabilities
- `job-state-resume-matching`: always-on trigger extended to `picture + compress-images`; explicit `jobStateMatched` semantics; phase marker task makes "state kept/removed on cancel" depend on the phase

## Impact

- `src/pipeline.cpp` (shouldEnableJobState, ensureJobState, maybeRemoveUnstartedCanceledJobState interplay)
- `src/core/app_context.h` (RuntimeContext.jobStateMatched)
- `src/core/job_state.h/.cpp` (phase marker task support)
- `src/picture/picture_process.cpp/.h` (cache lifecycle, quality key, resolveSource move, delete packAllPicsToZip)
- `src/picture/picture_compress.cpp/.h` (atomic outputs, finalization dedup, CompressResult slimming)
- `src/infra/stop_signal.h` (canceledExitCodeForPromptAbort fold-in)
- `src/video/video_process.cpp` (use the folded helper)
- `src/pack/pack.cpp` (applyEntryNameOverrides isSummary guard)
- Tests: picture_process_tests, pipeline_picture_tests, pack_execute_test, e2e (fake ffmpeg interruption/exit codes)
