# Design

## Context

See proposal.md for motivation. The current shape that constrains this design:

- `appentry::run` dispatches subcommands (`preview`, `organize`, `config`, `completion`) before the main encode workflow; `pipeline::run(AppContext&)` executes one single-directory run driven by `config.inputPath`, and creates its `jobstate::Store` lazily from `buildDefaultStateFilePath` → `resolveWorkRoot(config)`.
- Zip output resolution is hardcoded to `outputPath.value_or(<input root>) / "packed"` in three places: `resolveVideoPackOutputPath` (video encode), `runPicturePackWorkflow` (picture), and `runPackOnly` (pack-only, `src/app/pipeline.cpp`). Reusing `outputPath` to redirect batch outputs therefore double-nests (`packed/packed`) in all three.
- `jobstate::Store` matches a saved snapshot against the current `ConfigSnapshot`; one store per run, keyed by input paths — a shared store across subfolders would discard states on every sub switch.
- `resolveWorkRoot` prefers `outputPath` when set, so setting per-sub `outputPath` would also relocate each sub's `.encro` state/caches away from its source folder.

## Goals / Non-Goals

**Goals:**

- Reuse the single-directory pipeline, job state, and pack machinery unchanged per subfolder; the batch driver only orchestrates.
- Each subfolder's outputs, state, and caches stay independently resumable and independently failed.

**Non-Goals:**

- Per-subfolder option variation (one option set per invocation, by spec).
- Cross-subfolder parallelism (intra-pipeline parallelism already saturates the machine; rendering and stop signal are process-global).
- Mixed task types in one invocation.
- A batch-level ledger file (per-subfolder states already encode progress; the batch summary is derived live).
- Exposing the output-seam as a public CLI option on the main workflow.

## Decisions

### D1: `batch` as a subcommand with its own registration

`encro batch <root> [options]`, registered in `cmd.cpp` the way `organize` is: positional root, its own option set (the processing options the spec lists), config-key options following the existing subcommand registration pattern. Because CLI11 subcommands do not fall through to parent options, the batch subcommand registers explicit stubs for `-I`/`--state-file` and validates `-o` against the alias forms, so both fail with the native-style exclusion errors the spec mandates instead of a generic unrecognized-token error. Dispatch branch in `app_entry.cpp` before the main workflow. Rejected alternatives: a main-flow flag (`--each-dir`) entangles with `-I` multi-file semantics and the crowded main option table; extending `-I` to directories would silently change its current "multiple files, one job" contract.

### D2: In-process sequential driver over fresh per-sub `AppContext`s

The driver (new `src/batch/` module) enumerates eligible subfolders, then for each builds `AppConfig` = batch config with `inputPath = <root>/<sub>`, `inputPaths` cleared, per-sub `packOutputDir` (D3), `outputPath`/`stateFilePath` left nullopt; wraps it in a fresh `AppContext` (fresh `jobState` store, so each sub initializes/matches/discards its own state) and calls `pipeline::run`. Toolchain is resolved once and copied into each per-sub context. Each sub gets a fresh `videoInfoCache`: `AppContext` embeds the cache behind a `shared_mutex`, so it is neither copyable nor movable and cannot be shared across fresh contexts — ffprobe may re-probe a file that appears in two subfolders, which is cheap and rare. Rejected: subprocess fan-out (N processes × single-terminal rendering, no benefit); parallel in-process pipelines (CPU oversubscription against intra-pipeline workers, process-global stop signal and progress rendering); restructuring the cache into a shared pointer just to save rare re-probes.

### D3: Output redirection seam — new `AppConfig::packOutputDir`

All three zip-dir call sites (`resolveVideoPackOutputPath`, the picture workflow's output-dir resolution, `runPackOnly`) check a new optional `packOutputDir` field first and use it verbatim (no `/ "packed"` suffix); when unset, behavior is exactly today's. The batch driver sets `packOutputDir = <packedRoot>/<sub>`. `outputPath` stays nullopt so `resolveWorkRoot` keeps each sub's `.encro` state, segments, and caches under the subfolder itself — identical to a manual per-sub run, and state survives independently of where archives were written. Batch's `-o` is a plain directory path; the single-run output-alias forms (`+`, `=`, `input://`, `common://`) resolve against `inputPaths` the batch clears, so batch rejects them at parse time. Alternative rejected: per-sub `outputPath = <packedRoot>/<sub>` — produces `packed/packed` and relocates state/caches away from the source.

### D4: `ConfigSnapshot` records the optional `packOutputDir`

The snapshot gains the *optional* `config.packOutputDir` value (serialized only when set, mirroring the store's existing absent-field compatibility pattern — a field added later deserializes as absent so old states still match): single-run snapshots are unaffected, while a subfolder's saved state binds to its batch output location. Re-running the same batch resumes; changing `-o` discards and restarts per subfolder with the existing mismatch warning. Recording the resolved/effective path instead was rejected: an always-present value would mismatch and spuriously discard every pre-existing saved state on upgrade.

### D5: Media presence pre-check mirrors the effective pipeline scan

Before invoking the pipeline for a sub, the driver runs a cheap extension-based presence check that mirrors what the effective option combination would scan, not the bare task type: video encode → video extensions; picture + `--compress` → picture extensions; picture + `--video-webp` → pictures ∪ videos (a video-only subfolder is a task); picture plain pack → picture extensions; pack-only → any regular file. The check reuses the pipeline's own scan predicates — the module-private video set (`video_info.cpp`) and function-local picture set (`picture_process.cpp`) get exported helpers as part of this seam rather than the driver duplicating extension lists. Running the pipeline and guessing from "exit 0" was rejected because exit 0 cannot distinguish "did work" from "nothing to do" without extra plumbing, and the skip notice belongs to the batch's narration, not the inner pipeline.

### D6: Stop handling rides the existing signal

`stopsignal` is process-global. Within a sub, the existing cancellation path returns the cancellation exit code; the driver sees it, stops iterating, marks the remaining subfolders skipped, and returns the cancellation exit code itself. Between subs, the driver checks `isStopRequested()` before starting the next sub. No new stop plumbing.

### D7: Batch summary table

Plain text on stdout per `console-output-conventions`: one row per enumerated subfolder (`done` / `failed` / `skipped`), then a count line; failure exit code exactly when any subfolder failed. The process-level logging summary stays as-is (under batch it reports no job id — the batch table is the product-facing summary). Render-touching output self-checks via `ENCRO_DEBUG_DUMP_RENDER` per the repo convention.

### D8: Enumeration and exclusions

One `directory_iterator` pass over the root: keep first-level directories only, exclude hidden entries, `packed` (the packed root, whatever `-o` says — compared against the resolved packed root path), and `.encro`. Subfolders are processed in `directory_iterator`'s stable sorted order (`std::filesystem` iteration order is unspecified, so the driver sorts by name) for deterministic summaries and tests.

## Risks / Trade-offs

- [Resolver drift: one of the zip-dir call sites forgets `packOutputDir`] → All three resolutions (video, picture, pack-only) go through the seam in the same task group; unit tests cover the unset case (unchanged) and the redirected case for each of the three paths.
- [Mismatch-warning spam when re-running a batch with a different `-o`] → Each sub legitimately restarts; the warning is per-sub and consistent with existing discard semantics. Accepted.
- [Fresh `videoInfoCache` per sub re-probes a file present in two subfolders] → ffprobe is a fast local call and cross-sub duplicates are rare; restructuring the cache for sharing was rejected as unneeded machinery.
- [Skip pre-check and the pipeline's own scanner disagree on an exotic extension] → The pre-check calls the same exported scan predicates the pipeline uses, so a divergence requires editing two call sites of one predicate; a divergence can only cause a redundant "no media" inner notice, not wrong processing. Accepted.
