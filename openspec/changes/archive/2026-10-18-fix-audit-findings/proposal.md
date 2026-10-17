# Proposal

## Why

A full-repo audit found fourteen defects that share one property: they fail silently, or they fail at a layer far away from the cause. A changed `--crf` is reported as "already complete" and never re-encodes; a truncated probe-cache file kills the run before a single encode; path identity is case-folded, so on Linux two files collapse into one job-state task and a resume can report a never-encoded file as done; a model file with the wrong size is never repaired; one unreadable subdirectory aborts the whole pack with a raw exception; the state file is replaced non-atomically; the video extension whitelist refuses explicitly named `.webm` inputs with exit code 0; a cuDNN extraction is never verified; `-z` packs encro's own work directory into the archive; and a prompt that cannot read an answer is reported as a user cancellation. Each defect is small on its own. Together they decide whether a batch run's exit code and output can be trusted, which is the whole product.

## What Changes

- **Encoding settings join job-state matching.** The video codec, `--crf`, `--preset` and `--min-vmaf` join the config snapshot, so a run with different settings no longer resumes a state that was written with other settings. Picture quality stays out: it changes no video output's bytes, and the conversion cache it does affect is already keyed by quality.
- **State persistence is crash-safe.** The snapshot is replaced by renaming the staged file over a `.bak` of the previous one, so no window exists in which the state file is absent.
- **A malformed probe cache is discarded, never fatal.** A cache file that parses but lacks its `entries` array is treated like unparseable JSON: discarded and re-probed.
- **Path identity follows the filesystem's case rule.** Task ids, grouping keys and hashes case-fold only on platforms whose filesystem does, so two distinct Linux paths stay two distinct tasks.
- **A model file that fails its pin is repairable.** `--download-models` re-downloads a file that exists with the wrong size, instead of skipping it and reporting success.
- **cuDNN extraction is staged and verified.** Each DLL is written to a staging name, checked for a complete write, then renamed — the DLL the install check tests is renamed last, so an interrupted extraction cannot look installed.
- **No directory walk aborts a run.** Every walk (packer, organize pre-pass, ffmpeg/ffprobe search) tolerates unreadable entries and vanished files, reporting them the way the media scanner already does.
- **The video extension whitelist covers the containers the pack whitelist already recognises.** `.webm`, `.m4v`, `.ts`, `.mpg`, `.mpeg`, `.3gp` and `.m2ts` become video inputs instead of being silently ignored.
- **Packing never ingests encro's own work directory.** A directory pack skips `<root>\.encro`; other dot-prefixed entries stay eligible.
- **A prompt with no readable stdin fails the run instead of faking a decline.** EOF prints an actionable message and exits with a plain failure code.
- **A dry run reports what it would do.** Organize's `--dry-run` resolves the same destination decisions as a real run and words the totals as "would copy".
- **A scan that filters files out says so.** Already-HEVC video files skipped by a directory scan print one hint line instead of only a debug log.
- **Dead code and duplicated helpers go.** `ZipWriter` (unused) is deleted, and `findFFprobe`/`findFFmpeg` collapse into one resolver.

## Capabilities

### New Capabilities

- `path-identity`: the one rule for when two filesystem paths name the same file — the case-folding rule that task ids, pack grouping keys, hash keys and organize directory keys all derive from.

### Modified Capabilities

- `job-state-resume-matching`: the settings that change an encoded output participate in state matching; state persistence never leaves the state file absent when a commit completes.
- `video-encode-probing`: a probe cache that cannot be read in full is discarded, never a fatal error.
- `media-scan`: the video container set covers every container the pack whitelist already treats as video.
- `pack-media-store`: a directory pack never packs the program's own work directory.
- `image-character-organize`: an existing file that fails its size pin is repaired by `--download-models`; a cuDNN install completes or does not exist; `--dry-run` reports the copy/skip counts it would produce.
- `console-output-conventions`: a confirmation prompt that cannot read stdin is a failure, not a decline.
- `pipeline-narration`: files a scan filters out by policy are named in one hint line.
- `error-visibility`: the directory-walk failure rule covers every walk in the program, not only the media scanner.

## Impact

- Code: `src/core/job_state.{h,cpp}`, `src/core/collision_naming.h`, `src/video/probe_cache.cpp`, `src/video/video_info.{h,cpp}`, `src/tagger/model_store.cpp`, `src/pack/packer.cpp`, `src/organize/{execute,disposition,teach,report}.{h,cpp}`, `src/organize/pipeline.cpp`, `src/batch/batch.cpp`, `src/picture/picture_process.cpp`, `src/utils/utils.{h,cpp}`, `src/core/media_scanner.{h,cpp}`, and the three prompt call sites in `src/video/video_process.cpp`, `src/video/video_batch_execution.cpp` and `src/picture/picture_process.cpp`.
- Tests: new cases in the existing suites (`tests/job_state_tests.cpp`, `tests/video/probe_cache_tests.cpp`, `tests/video_info_tests.cpp`, `tests/tagger/model_store_tests.cpp`, `tests/pack_execute_tests.cpp`, `tests/packer_tests.cpp`, `tests/naming_strategy_tests.cpp`, `tests/organize/{pipeline,disposition}_tests.cpp`, `tests/media_scanner_tests.cpp` (`[media-scanner]`), `tests/utils_tests.cpp`, `tests/cmd_config_builder_tests.cpp`), plus a new `tests/path_identity_tests.cpp` for the shared identity rule.
- No new dependencies. No on-disk schema change: the job-state and probe-cache documents keep their version and fields, so states written before the change still load. The only path-derived names that change are conflict-handled file names and segment-directory hashes on case-sensitive platforms, where an in-flight job re-encodes from scratch and leaves its old segment directory orphaned.
- Three user-visible behaviour changes, all narrow: `-z` of a work root no longer archives `.encro`; a closed-stdin prompt exits 1 with a message instead of "canceled by user"; a directory scan that skips already-HEVC files prints one hint line.
