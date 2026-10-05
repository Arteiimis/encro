## Why

Real galleries are not virgin directories: by the time the user reaches for `organize`, most of the collection already sits in per-character folders the user built or renamed by hand, and what actually needs sorting is the remainder — loose images at the root plus one or two miscellaneous folders. The command assumes the opposite: teaching reads only the children of `organized/`, so the user's top-level character folders are invisible to it (non-recursive runs ignore them; `-r` runs grind them up as input), and every run builds a parallel structure under `organized/` instead of adding to the structure that is already there. The feature's real job is incremental: evaluate the remainder against the existing structure and file into it, touching nothing that already exists.

## What Changes

- **Incremental mode, entered automatically** when a non-recursive run's target directory has first-level subfolders besides the output tree (`organized/`): one notice line names the mode and the reference folders found. Recursive runs keep the whole-set behavior they always had — every first-level folder becomes input, nothing is mirrored — so a nested gallery with no per-character structure still organizes as before. A non-recursive directory with no such subfolders behaves exactly as before.
- **Every first-level folder gets a disposition**, decided in this order: explicit flags (`--ingest <name>` — treat as input; `--ignore-folder <name>` — skip entirely) beat the built-in miscellaneous-name list, and the list beats the default. The list holds common miscellaneous folder names in English, Simplified Chinese, Traditional Chinese and Japanese (`mix`, `misc`, `杂项`, `未分类`, `その他`, …), matched exactly (trimmed, ASCII case-insensitive, no substring matching, no simplified/traditional conversion); a hit means the folder's images are input. Dot-prefixed first-level folders are ignored unless `--ingest` names them. Everything else is a **reference**.
- **Reference folders are mirrored, not touched**: matched images copy into `organized/<the reference folder's own name>/` (CJK names intact). Skeleton folders are created lazily — only when an image actually lands in one — while the report lists every reference folder including zero-match ones. The root's folders and images are never modified; copy-only semantics are unchanged.
- **References get profiles by sampling**: up to a fixed number of members per folder (members collected recursively, ordered by content hash) are analyzed and cached in the same content-hash cache, so a first-level folder teaches tag ownership and folder matching exactly as an `organized/` child does, and re-runs pay nothing.
- **Mixed-content detection**: a reference whose sample splits into two or more identity clusters is demoted — it neither matches clusters nor owns character tags — and the report hints `--ingest` for it. Demotion never auto-ingests.
- **Input is the remainder**: root-level loose images plus `--ingest` folders' images (collected recursively), routed by the unchanged tag/clustering/folder-match machinery; unmatched images open `unknown_*` folders as today. New folder names are seeded with existing folder names so a new cluster can never collide with an existing folder.
- **Behavior change, non-breaking**: non-recursive runs over directories with first-level folders previously ignored those folders entirely (loose images organized with no knowledge of them); they now become references and mirrors, so some images land in different destination folders than before. `-r` keeps its whole-set behavior, and no output the previous release produced is deleted or renamed.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: one added requirement (incremental organize over an existing folder structure — mode entry, folder dispositions, sampling, demotion, skeleton mirroring) plus modified requirements for the command surface (the two new flags and the incremental input set), teaching (reference sources extended to first-level folders, with sampling and demotion), and the report (disposition lines and zero-match reference rows).

## Impact

- `src/organize/organize_command.cpp` — the disposition pre-pass and the incremental-entry notice run here, before engine loading (they need no models); the computed dispositions flow into the run.
- `src/organize/pipeline.cpp` — consumes the dispositions: the reference-sampling stage (with its own progress bar) and the seeding of used cluster names.
- `src/organize/execute.cpp` — a two-line reorder so a destination folder is created only after its staging copy succeeds (strict lazy creation; a failed copy leaves no empty folder).
- `src/organize/teach.{h,cpp}` — reference construction reads first-level folders in addition to `organized/` children and merges same-name sources; sampling feeds `accumulateMember` as analyses arrive.
- `src/organize/scan.{h,cpp}` — input-set assembly: root loose images plus recursively collected ingest-folder images, excluding reference/ignored folders and the output tree.
- `src/organize/organize_types.h` — `Options` gains the ingest/ignore lists and the sample-size constant (with its calibration note); `src/cmd/cmd.{h,cpp}` gains `--ingest`/`--ignore-folder` (repeatable).
- `src/organize/report.cpp` — disposition summary and zero-match reference rows.
- `tests/organize/` — disposition rules, misc-name list boundaries (case, trim, substring rejection, CJK exactness), sampling determinism, demotion, skeleton naming and collision seeding; `tests/e2e/encro_organize_tests.cpp` — one incremental end-to-end run with pre-existing root folders.
- `README.md` — the incremental behavior, the disposition rules and the two flags.
- Cost: first-run sampling inference (sample size × reference folders; cache makes re-runs free); no new model files, no cache-format change, no new config keys.
