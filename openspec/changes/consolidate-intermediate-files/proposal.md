# Consolidate Intermediate Files

## Why

Encoding scatters intermediate files across four roots with ad-hoc, per-file-type cleanup: the OS temp root (`progress_*.txt`, `vmaf_*/ssim_*` logs, `encro_probe_*` and `encro_preview_probe_*` dirs), `%TEMP%\encro\` (segments, job-state fallback, POSIX logs), `%LOCALAPPDATA%\encro\` (logs, probe cache), and the output/input directories (`.compress_tmp_q*`, `.partial`, `encro.job-state.json`). The WebP encode path leaves a `progress_*.txt` file behind after the final attempt, crashed runs leave unreachable probe/segment dirs in the OS temp root with no startup sweep to reclaim them, and the recursive media scanner does not skip dot-directories, so the program's own `.compress_tmp_q*` caches can be re-scanned as input on a second run. The layout needs one managed policy: per-run scratch, per-run resume data, and long-lived persistence each in a single well-defined place, with a visible work root the user can identify and clean.

## What Changes

- Add `%TEMP%\encro\scratch\` as the single home for per-run transient files: encode progress files, VMAF/SSIM logs, CQ probe dirs, and preview probe dirs. A startup sweep removes stale leftovers from crashed runs; the existing per-file cleanup stays for the graceful path.
- Introduce a hidden `.encro\` directory at the **output root** (dot-prefix on POSIX; `Hidden` attribute on Windows — new platform shim, none exists today) that consolidates all encoding intermediates rooted at the output:
  - `segments\{hash}\` — video segments, per-segment progress, extracted audio, concat list (moved from `%TEMP%\encro\segments_*`; kept on interruption for resume, removed on success).
  - `compress_q{N}\` — picture compression cache incl. `.partial` staging (moved from `.compress_tmp_q{N}`; name still encodes quality).
  - `job-state.json` with its atomic-write staging `job-state.json.tmp` — job-state file (staging already lives next to the file today; location and naming unchanged).
- `.encro\` anchor rule (reuses existing pieces, does not reuse the job-state path resolution): explicit `--output` → `<outputPath>\.encro\`; webp without `--output` → `<sourceRoot>\encoded_webp\.encro\`; single input directory → `<dir>\.encro\`; single input file → `<parent>\.encro\`; inputs spread across directories → `.encro\` at their common parent. The common parent SHALL be found; when inputs have no common parent (e.g. cross-drive inputs), the run fails with a clear error telling the user to pass an explicit `--output` — `%TEMP%\encro\` is never used as a work-root fallback.
- Media scanner skips dot-prefixed directories so `.encro\` (and legacy `.compress_tmp*` siblings) are never re-scanned as inputs.
- Default job-state file path moves to the hidden `.encro\` per the anchor rule (e.g. `<output>\encro.job-state.json` → `<output>\.encro\job-state.json`). **BREAKING** for tooling that hard-codes the default path; explicit `--state-file` is unaffected.
- `%LOCALAPPDATA%\encro\` (logs, probe cache) and the picture-workflow's `packed\` output-dir layout stay unchanged.

## Capabilities

### New Capabilities

- `media-scan`: Input scanning excludes dot-prefixed hidden directories (`.encro\` and legacy `.compress_tmp*`), preventing a scanner from ingesting the program's own intermediates.

### Modified Capabilities

- `picture-compress-resume`: The compression cache directory relocates from `<outputDir>\.compress_tmp_q{N}\` to the hidden `.encro\compress_q{N}\` at the output root. The cache name still encodes quality; the "preserved on interrupt, removed on success, discarded on restart/non-matching state" semantics are unchanged. The spec wording "cache directory below the output directory" is adjusted to "cache directory within the hidden `.encro\` directory at the output root" so the new location is unambiguous (the cache is a sibling of `packed\`, not a descendant of it).

## Impact

- **Modules**: `src/video/video_encode_runner.cpp`, `src/video/segment_dir.h`, `src/video/encode_probe.cpp`, `src/video/video_quality.cpp`, `src/preview/preview_process.cpp`, `src/picture/picture_compress.cpp`, `src/picture/picture_process.cpp` (incl. the `.compress_tmp*` stale-sweep in `prepareCompressTempDir`), `src/core/job_state.cpp`, `src/core/job_state_store.cpp`, `src/core/media_scanner.cpp`.
- **New platform shim**: setting the Windows `Hidden` attribute on `.encro\` (POSIX needs nothing).
- **Tests**: unit/integration tests asserting temp-path layout (`tests/` segment_dir tests, picture/pipeline tests) and the e2e fake-tool harness assertions on progress/segment/cache paths; new tests for scanner dot-directory exclusion.
- **No CLI or config changes**; resume semantics, cache validity, and probe-cache behavior are preserved.