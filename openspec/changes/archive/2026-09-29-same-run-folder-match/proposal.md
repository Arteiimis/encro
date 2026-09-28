## Why

On the run that matters most — the first one — folder matching has nothing to match: the output root is empty, so the images the tagger did recognize are filed by routing while the unassigned remainder is clustered with no knowledge of those folders. A cluster of images belonging to a character who already has a tag-derived folder therefore receives an `unknown_*` name and only joins that folder on a later run, which is the behaviour a user sees as "it worked the second time". Measured on the labelled collection (3840 images across 46 characters, of which routing places 2443 by character tag, 81 in `mixed/` and leaves 1316 unassigned), end-to-end pairwise co-clustering F1 is 0.697 for the shipped pipeline, 0.739 after the two companion changes (corrected input preparation and calibrated fused agglomeration), and 0.819 once this run's own routing is used as folder-match references — against a measured ceiling of 0.854 for this gallery. That is the largest single lever left: the agglomeration itself was worth +0.03 end to end, this mechanism is worth +0.08, and it costs no inference because the evidence is already in the run. The pipeline already does exactly this on a second run: an acceptance pass over a copy of a 1300-image gallery reported 41 folders as folder matches on its second run, with folder counts settling between runs. The only thing missing is using, in the first run, what the first run has already discovered.

## What Changes

- Folder-match references gain a second source: after routing, the images this run assigned to one destination folder form a reference for that folder, exactly as an on-disk folder does today — the mean of their identity features (and of their identity-tag vectors once the fusion change lands) plus their character-tag ownership evidence, all read from analyses the run already holds.
- Where the two sources disagree the on-disk folder still wins: a folder that owns a character tag — including one the user renamed — keeps owning it, and this run's routing never renames, recreates or repopulates an existing output folder.
- A cluster that reaches the identity similarity threshold against such a reference is filed into that folder's name in the same run, instead of receiving an `unknown_*` name and waiting for a later run.
- Nothing else moves: routing order, `mixed/`, `unknown_*` naming, the work-stem grouping, the never-rename/never-delete contract, the copy semantics, the cache format, the models, the download set and the CLI surface are unchanged.
- **BREAKING (output)**: a single run over a gallery can now produce folders that the previous release only produced on a second run, so the folder set from one run of the new release differs from one run of the old one. Nothing is moved or deleted, and every image is still copied exactly once.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: a new requirement adds the second reference source — folder matching also uses this run's own character-tag assignment, with the existing on-disk ownership rule still taking precedence — together with the scenarios that pin the first-run capture, the renamed-folder precedence, the threshold, and the no-routing fallback. The existing teaching requirement is left as it stands (it is already being modified by the in-flight fusion change, and two deltas editing one requirement would have to be merged on archive); the new requirement names the reference profile in one clause and defers to the teaching requirement for the comparison — the same weighted combination, the same separately calibrated default when a side carries no identity-tag evidence, and the same threshold.

## Impact

- `src/organize/pipeline.cpp` — the routing already records each filed image's destination on the item itself (`folderName` and `folderSource`, set in `fileByCharacter` `:33-47`, with `mixed/` and `uncategorized/` set the same way at `:82` and `:193`), so this change selects the items whose source is a character folder and builds the reference set in `runOrganize` (`:373-375`) alongside `buildFolderReferences`; the folder-match step (`:227`) consumes the merged set unchanged.
- `src/organize/teach.{h,cpp}` — reference construction gains an item-list source reusing `accumulateMember` (`teach.cpp:17`) instead of reading only folder contents (`teach.cpp:47`), and the merge that gives an on-disk reference precedence over a routing-derived one for the same name lives here; `FolderReference` (`assign.h:82`) itself does not change, and neither does the teaching requirement's own text.
- `src/organize/assign.{h,cpp}` — unchanged: `claimedTag`/`owningFolder` (`assign.cpp:61-86`) are consulted exactly as they are today, and because a folder that owns a tag already decides the destination, the on-disk reference wins the merge by name.
- `tests/organize/routing_tests.cpp`, `tests/organize/pipeline_tests.cpp` — a first-run capture case (nothing on disk, a cluster matching the profile of a character whose images were tag-routed in the same run) and a precedence case (a renamed folder owning the tag beats the routing-derived reference); `tests/e2e/encro_organize_tests.cpp` — one first-run end-to-end case whose images land in the tag folder with no pre-existing folder on disk.
- `README.md` — one sentence that a first run can now file a cluster into the folder its own character-tag routing created.
- Cost: one extra pass over analyses the run already holds — no inference, no new model file, no config key, no cache-format change.
- Depends on `fuse-tags-and-average-linkage`: the numbers above are for the fused score with agglomeration, and the reference profile shape (feature mean plus tag mean) comes from that change; it also inherits that change's dependency on `clip-normalize-identity-input`.
