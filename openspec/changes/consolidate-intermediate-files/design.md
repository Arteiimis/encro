## Context

Current layout scatters intermediates across four roots with per-file-type cleanup: the OS temp root (`progress_*.txt`, `vmaf_*/ssim_*`, `encro_probe_*`, `encro_preview_probe_*`), `%TEMP%\encro\` (`segments_*`, job-state fallback, POSIX logs), `%LOCALAPPDATA%\encro\` (logs, probe cache), and the output/input directories (`.compress_tmp_q{N}`, `.partial`, `encro.job-state.json`). Key constraints discovered during exploration and verified against code:

- Job-state atomic-write staging already lives next to the state file (`makeTempStatePath` = `parent_path()/{name}.tmp`, `src/core/job_state.cpp:56-57`) — no cross-volume bug exists; only the WebP path leaks a progress file.
- The job-state default-path resolution (`buildDefaultStateFilePath`) places single-directory inputs at the input's *parent* directory, which leaks outside the output root — it cannot be reused as the `.encro\` anchor rule.
- The webp-without-`--output` output root (`<sourceRoot>/encoded_webp`) is not covered by the job-state resolution at all.
- `media_scanner` skips nothing (`kDirectoryOptions = skip_permission_denied` only), so a second recursive run can re-ingest `.compress_tmp_q{N}` as input.
- The picture compression cache already contains the `.partial` staging files (compress outputs go to the cache dir, `src/picture/picture_process.cpp:167/473`), so consolidating the cache also consolidates the staging files by construction.

Motivation and scope: see proposal.md — Why / What Changes.

## Goals / Non-Goals

**Goals:**

- One per-run scratch root (`%TEMP%\encro\scratch\`) that the program may clean wholesale at startup.
- One hidden `.encro\` directory at the resolved output root holding segments, picture caches, and job state — the user-visible "work root" for a run.
- An anchor rule that always resolves to a real directory (no temp fallback), failing with a clear error when inputs have no common parent.
- Scanner exclusion of dot-prefixed directories so the program never re-scans its own intermediates.
- Windows `Hidden` attribute on `.encro\` (dot-prefix is invisible only on POSIX).

**Non-Goals:**

- Moving `%LOCALAPPDATA%\encro\` (logs, probe cache) or changing the picture `packed\` product layout.
- Changing the `.partial`/`job-state.json.tmp` atomic-write mechanism (same-directory rename already correct).
- Adding automatic expiry cleanup for abandoned segment dirs (status quo: kept until a resume consumes them; revisit later).
- Migrating leftovers from old versions (temp-root strays and old `.compress_tmp*` dirs are reclaimed best-effort, not migrated).

## Decisions

### D1. `.encro\` anchor = output root via a new resolver, not the job-state path resolution

New helper `resolveWorkRoot(config, inputPaths) -> eh::Result<fs::path>`:

1. explicit `--output` → `<outputPath>`.
2. webp without `--output` → `<sourceRoot>/encoded_webp`, where `sourceRoot` is the input root (single root, or the lowest common ancestor of the inputs).
3. single input directory → that directory; single input file → its parent.
4. multiple inputs → their lowest common ancestor.
5. empty input list or inputs with no common ancestor (e.g. cross-drive): error telling the user to pass `--output` — `%TEMP%\encro\` is never used as a work-root fallback.

Rationale: reusing `buildDefaultStateFilePath` was rejected because (a) its multi-input branch only anchors when inputs share one parent (a same-parent check, not a true lowest common ancestor) and falls back to `%TEMP%\encro\jobs\` otherwise — exactly the fallback this change forbids; (b) it has no webp/`encoded_webp` branch; (c) its empty-input fallback would also point at temp. The new resolver adds the missing branches and turns unresolvable cases into explicit errors. `buildDefaultStateFilePath` is re-derived from the new resolver so job state follows the work root automatically.

### D2. Scratch (`%TEMP%\encro\scratch\`) stays on the OS temp volume; resume data goes to `.encro\`

Scratch (progress files, VMAF/SSIM logs, probe/preview dirs) is single-process, transient, and small — it stays on the temp volume, grouped under `%TEMP%\encro\scratch\`, and a startup sweep clears entries untouched for > 24h (age threshold prevents a concurrent run's live files from being deleted). Segments are large and resume-critical; they move to `<work-root>\.encro\segments\{hash}\` per the user decision, so deleted-output-directory cleanup removes them along with their products.

Alternative considered: putting scratch inside `.encro\` too — rejected, it would inflate the user-volume footprint with data that never needs to outlive the process and would mix two lifecycle classes in one directory.

### D3. Startup sweep scope and concurrency guard

Sweep deletes only entries under `%TEMP%\encro\scratch\` whose last-write time is older than 24h (probe/progress files are rewritten on every run, so a live run's files are always fresh). This is deliberately conservative: correctness over reclaiming disk. Segment dirs under `.encro\segments\` are never swept (resume data; removal is per-task on success).

### D4. One new header owns all work-directory paths (`src/core/work_dirs.h`)

`resolveWorkRoot`, `scratchDir()`, `.encro` subpaths (`segmentsDir(hash)`, `compressCacheDir(quality)`), and the Windows `Hidden` shim (`SetFileAttributesW(FILE_ATTRIBUTE_HIDDEN)`, applied when creating `.encro\`; no-op elsewhere) all live here. Every caller replaces its inline path construction: `video_encode_runner.cpp`, `segment_dir.h`, `encode_probe.cpp`, `video_quality.cpp`, `preview_process.cpp`, `picture_process.cpp`, `job_state.cpp`. Rationale: the whole point of the change is that paths stop being assembled ad-hoc at call sites; a single header is the anti-regression mechanism. `segment_dir.h` shrinks to thin wrappers over `work_dirs.h`.

### D5. Scanner exclusion via dot-prefix check

`media_scanner` skips any directory or file whose filename starts with `.`, both in recursive (use `disable_recursion_pending()` on dot-directories) and non-recursive scans. This covers `.encro\` and legacy `.compress_tmp*` in one rule. No warnings are emitted for skips (silent by design).

### D6. Cache rename and legacy sweep

`buildCompressCacheDir` returns `<work-root>\.encro\compress_q{N}\`. `prepareCompressTempDir` sweeps stale `compress_*` entries inside `.encro\` and additionally removes legacy `.compress_tmp*` dirs under `<work-root>\packed\` (the historical cache location; the old cache always lived inside `packed\`, not at the output-root top level).

## Risks / Trade-offs

- **GB-scale segments now occupy the user volume** → terminal-summary note and docs; interruption keeps them deliberately (resume value), success removes them.
- **Concurrent runs could delete each other's scratch** → 24h age threshold in the sweep; fresh files are never touched.
- **Default job-state path moves (BREAKING)** → explicit `--state-file` unaffected; document in CHANGELOG; resume uses the same resolver so default runs stay self-consistent.
- **Cross-drive inputs with no common parent now hard-fail** → new, explicit error message telling the user to pass `--output`; previously the state file silently fell back to temp.
- **Scanning behavior change could skip legitimate dot-prefixed user media** → dot-prefix is the established hidden-file convention; documented in the media-scan spec.

## Migration Plan

No data migration: all intermediates are ephemeral by definition. Steps: (1) implement resolver + work_dirs.h; (2) repoint call sites; (3) scanner exclusion; (4) startup sweep; (5) legacy `packed\.compress_tmp*` removal inside `prepareCompressTempDir`; (6) docs/CHANGELOG note on the default state-file path. Rollback: single revert — old path construction is isolated per call site and the new files are additive.

## Open Questions

- None that would change the specs, approach, or task breakdown. (Segment-dir expiry is deferred by Non-Goals.)