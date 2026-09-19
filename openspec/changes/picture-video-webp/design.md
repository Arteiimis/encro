## Context

See `proposal.md` - Why. What shapes the approach:

**The picture workflow today.** `-t picture <dir>` scans image extensions only (`readAllPics` in `src/picture/picture_process.cpp`), optionally compresses each picture to JPEG (`-c`) into `<work-root>\.encro\compress_q{N}\`, then packs every entry into `packed\pics_partN.zip` with flat entry names. The scan never sees a video, so clips are neither converted nor packed.

**The video workflow already converts to animated WebP.** `-t video -f webp` builds its command in `EncodeConfig::buildCMD()` (`-vf "scale=-2:960:force_original_aspect_ratio=decrease" -c:v libwebp -q:v Q -loop 0`), searches quality downward in `video_encode_runner.cpp` (start q=80, steps -10 / -5, floor q=20, target <20MB), gates inputs at 32MB (`kWebpInputMaxSize` in `video_info.cpp`), and skips outputs of 20MB or more when packing (`video_process.cpp`). That recipe is the feature's encoder; this change must not fork it.

**Resume exists at two granularities.**
- Picture compression: one phase record (`compress-phase`) plus an on-disk cache directory whose name encodes the quality; per-file reuse is an mtime check against the cached JPEG.
- Video encodes: one record per file, `encode:<path>` (kind `encode_video`), with restore-from-existing-output, Running→Interrupted normalization, and a source fingerprint over path, size and mtime.

**Measured on the triggering input** (7.4 s, 1664x1024, 30fps, 7.9MB H.264): the recipe's first tier (q=80, 960p) yields a 10.5MB WebP in ~20 s; q=30 yields 5.0MB in ~17 s; native-resolution 30fps yields 10.2MB in ~28 s. Animated WebP is a much weaker codec than H.264, so the converted file is typically not smaller than the source. Conversions are also 50-100x slower than a picture compression, which drives the state and progress decisions below.

**Shared config.** Picture and video runs share `appctx::AppConfig`, whose `outputFormat` is the video-mode output selector (`mp4`|`webp`) and is part of the job-state config snapshot; picture runs leave it at `mp4`.

## Goals / Non-Goals

**Goals:**
- One run, one set of archives: pictures and converted clips under one naming scheme.
- The same clip converts to the same output through either workflow (one recipe, one call path).
- Conversion work survives cancellation at per-clip granularity; finished clips are never converted twice.
- With the flag absent, the picture path behaves exactly as it does today.

**Non-Goals:**
- A second WebP implementation or recipe, or a quality knob for it.
- Generalizing the video runner's segment/progress-file machinery into a shared abstraction.
- Making converted outputs smaller than their sources, or ordering archive entries by name.
- Persisting the flag as a user-config key: `user-config` enumerates the accepted key set, so a key would mean a second spec delta and `config set/get` plumbing this change does not need.

## Decisions

### D1: Call the existing WebP encoder through one new entry point

Add a WebP entry point to `video_encode_runner.h` (for example `encodeVideoToWebp`) that runs the same internal adaptive-quality path `encodeVideo` uses for `-f webp`. `encodeVideo`'s own WebP branch becomes a call to that entry point, so both workflows execute one implementation and one recipe.

The format must be passed in, not read from the config: `runWebpEncodingStep` builds `EncodeConfig{.outputFormat = appCtx.config.outputFormat}` and `encodeVideo` branches on `ctx.config.outputFormat == "webp"`, while picture runs keep that field at `mp4` (it selects the video output format and feeds the job-state snapshot). The entry point therefore takes the output format explicitly (or is defined as the WebP one) and the internal step builds its `EncodeConfig` from that value. The picture phase builds an `appctx::EncodingState` per video (input path and the temporary output path, see D5) and never touches `ctx.config.outputFormat`.

- *Alternative: force `config.outputFormat = "webp"` for the conversion phase.* Rejected: that field is part of the job-state config snapshot (`jobstate::buildConfigSnapshot`), so a picture run marked `webp` would be judged a different job from the same run later, breaking resume matching between a conversion run and a plain picture run. (The work-root anchor rule is not at risk either way: `work_dirs.cpp` only takes the WebP anchor when `processType == "video"`.)
- *Alternative: write a second ffmpeg command for the picture path.* Rejected: two recipes drift, and the spec requires identical parameters between the two workflows.

### D2: One job-state task per video, not a phase record

Conversions register `jobstate::makeEncodeTask(videoPath, convertedOutputPath)` records, exactly as video encodes do. `needsExecution`, the Running→Interrupted normalization, restore-from-existing-output and the source fingerprint then give the required resume behavior with no new task model; the phase adds only the file-level freshness purge D3 describes.

- *Alternative: mirror the picture phase record (`compress-phase`) plus an mtime check in the cache directory.* Rejected: at ~20 s per clip a phase record re-does every unfinished clip, and the picture path's mtime check is only sound because pictures are written through a temp file (see D5).

### D3: Conversion cache at `<work-root>\.encro\webp\`

The conversion cache is a sibling of the picture compression cache and is not keyed by `-q/--image-quality`: the WebP recipe has no quality input, and the flag must work with `-c` off, when no `compress_q{N}` directory exists at all. Lifecycle mirrors the picture cache: kept when a run is canceled or interrupted after conversion started, removed when the run completes successfully, emptied on `--restart` or config mismatch.

Validity inside the cache needs two legs, because the job state alone cannot carry it. The first is the task fingerprint (source path, size, mtime): a changed source resets its record on state load. The second is a file-level rule the phase applies before it reconciles tasks with the store: a clip whose saved record is not already a succeeded conversion of the current source has its cached output discarded - a record reset because its source changed counts as not succeeded, as does a missing, interrupted or failed record. Without that purge, a replaced source whose old WebP is still on disk would be restored from the stale file and packed, because the fingerprint reset and the restore decision happen at different moments and `normalizeExistingTask` promotes any task whose target exists. The purge must precede the merge, not follow it.

The fingerprint is what decides "the source is unchanged", so no separate age comparison is needed: it covers the case the picture path's `addCompressTask` age rule (cached output older than source, or recompress) would catch, and also a source that was replaced by a file with an older timestamp, which an age comparison alone would keep.

An empty cache on a run that does not trust the cache (no matching saved state, or `--restart`) covers the remaining case. A future recipe change must take a version suffix in this directory name so old outputs cannot be mistaken for new ones - recorded as a pitfall, not built now.

### D4: A dedicated video scan for picture runs; do not reuse `readAllVids`

`videoinfo::readAllVids` post-processes according to `config.outputFormat`, which in a picture run is `mp4`, routing through `finalizeVideoList`: that runs one ffprobe per video and **skips already-HEVC-encoded files** ("Skipping already HEVC encoded file") because re-encoding HEVC to HEVC is pointless in video mode. For this feature both effects are wrong - an HEVC source must still convert, and the probe result is unused because the recipe's only dimension-dependent value is the fixed height cap.

So the picture path gets its own scan: the video extension set (moved out of the anonymous namespace in `video_info.cpp` so it stays single-sourced) plus `media::scanByExtensions`, followed by the same 32MB WebP input limit, with no ffprobe. Unlike the video path, which reports that limit at debug level only, the scan warns on the console: a picture run that silently drops a clip the user asked to convert would look like the flag did nothing for it.

- *Alternative: call `readAllVids` with a copied config whose `outputFormat` is `"webp"`.* Rejected: it still spawns a probe per clip for a prewarmed cache nothing reads, and it hides the intent behind a mutated config.

### D5: Converted outputs are finalized by rename, so their final path is always complete

Picture compression writes `<name>.partial.jpg` and renames on success. Conversions do the same: the phase points the encoder at a temporary output path carrying a `.webp` extension (mirroring `picture-compress-resume`'s "Temporary outputs keep a recognizable media extension") and renames it to the final cached name only after the encoder process reports success. Job-state task targets name the final path.

This is not decoration. `jobstate::normalizeExistingTask` promotes a `Pending` or `Interrupted` task to `Succeeded` whenever its target file exists, so a record that lost its `Running` status mid-write would otherwise let a truncated WebP be restored as a finished conversion and packed. With the rename, a file at a final converted path is complete by construction, and the rename is also what makes the ordinary interrupted case (a `Running` record normalized to `Interrupted`) re-run cleanly.

- *Alternative: trust the task status alone (write the final path directly, as `-t video -f webp` does).* Rejected for this path: it needs the `Running` status to be persisted before the encoder starts and to survive the crash, which is exactly the assumption `normalizeExistingTask` does not make.

### D6: Conversion is its own phase, between picture compression and packing

Order per run: scan pictures → scan videos (each reporting its own count line) → confirm → [picture compression] → [video conversion] → pack. Packing starts only after every conversion settles, so no archive is written from a half-converted set.

- *Alternative: one shared worker pool for both kinds of work.* Rejected: a conversion occupies a slot for ~20 s while a picture takes ~0.3 s, so the picture bar's completion count freezes and the pools' different failure and concurrency rules would have to be merged.

Progress: the conversion phase owns one `progress::ProgressContext` bar (`Converting videos: n/m`) with the tool's retry status in the postfix; `--full-progress` follows the existing per-item convention. The phase prints nothing when the scan found no videos, so a flag with no clips in the input adds no empty line.

### D7: Conversion concurrency is capped at a small constant

Conversions run at most `min(-j/--jobs, 2)` at a time. Each conversion is a single-core libwebp encode plus an ffmpeg decode, so a picture-style size-derived cap (up to 6) would oversubscribe the machine while gaining little: throughput is bounded per conversion, not per machine.

### D8: The converted file replaces the source clip, with no output size gate

A successfully converted clip is packed as its WebP; the source is not packed, even when the WebP is larger (the measured common case). The video workflow's 20MB post-encode packing gate is deliberately not adopted: the user asked for these clips, and dropping an oversized conversion would silently omit exactly what the flag promises. The 32MB input gate bounds the worst case, and the adaptive search's minimum-quality fallback already keeps the output as small as the recipe can make it.

The pack entry carries the clip's own directory as its `sourceDir`/`sourceKey` while its `sourcePath` is the cached WebP. Picture entries already do this (they key off the picture's directory, not the compression cache), and the picture pack groups by that key — `PerSourceDirKeepTogether` resolves to a keep-together threshold of 0, so every entry passes through the source-key-ordered grouping in `pack.cpp`. Keying a converted clip by its cache directory instead would group it away from its pictures and could split it into an archive of its own.

### D9: Failure semantics mirror picture compression

Per-clip failure is reported with its reason and the clip is left out; other entries still pack; when every conversion fails the run aborts before packing instead of writing an archive that silently lacks the requested clips. No failure retry is added: the encoder's own quality search is the only re-attempt, and a clip that fails the search is reported rather than retried. This is a deliberate mirror of the existing picture rules, so a reversal is a one-condition change. A directory holding clips but no pictures keeps failing with the existing "No pictures found" error — the flag converts what a picture run already found, it does not turn the run into a video run.

### D10: Job state enabling and snapshot field

`shouldEnableJobState` (in `src/app/pipeline.cpp`) gains `videoWebp` alongside `compressImages` for picture runs, and `jobstate::ConfigSnapshot` records the flag (`videoWebp`), with `configMatches` requiring equality; `job-state-resume-matching` holds the matching requirements.

### D11: CLI surface

One new option, `--video-webp`, registered in the processing group next to `-c/--compress`, visible in the default help tier, bound to a new `appctx::AppConfig` boolean via `CmdParseResult`, rejected outside `-t picture` in `applyMediaOptionValidations` the way `--compress` already is. No `--no-video-webp` negation and no config key: the negation form exists for flags whose defaults can be injected from the config file, and this flag has no config entry.

## Risks / Trade-offs

- **Converted clips are usually larger than the sources** (measured 10.5MB WebP from a 7.9MB clip) → accepted: the feature buys format consistency and one browsable set, not size. The recipe's height cap is the single knob if that changes.
- **Conversions are slow** (~20 s per 7 s of video, single-threaded) → own phase, own bar, small concurrency cap, and per-clip resume so the cost is paid once.
- **ffmpeg builds without libwebp** → each clip reports the tool's own diagnostic; the all-failures rule turns a wholesale failure into a run error rather than an archive missing every clip.
- **Recipe drift leaves a stale cache** → cache validity rests on the job-state fingerprint plus the phase's source-newer-than-output purge (D3), so a changed recipe needs the versioned directory name described there.
- **A partial output can be mistaken for a complete one** → avoided by finalizing conversions through a rename (D5): a final converted path only ever holds a complete file, whatever the task record says.
- **Bigger archives from a small input change** → the conversion phase's line names the conversion count before it runs, so the added cost is visible up front.

## Migration Plan

No data migration. The job-state file gains one optional field; a state lacking it reads as conversion-off, so enabling the flag treats an old state as mismatched and starts clean (the conservative direction). Rollback is removing the flag's plumbing; caches left in `<work-root>\.encro\webp\` are inert hidden state and are removed by the next successful run of the feature or by `--restart`.

## Open Questions

- Should `--video-webp` become a user-config key later? It would need `user-config`'s enumerated key set updated plus `config set/get` coverage; nothing in this design blocks it.
- Should archive entries be ordered so clips interleave with pictures by name? Today's order is scan order, which is not name order for pictures either; changing it would touch the picture pack input list, and no requirement depends on it.
