## Context

See `proposal.md` — Why for the motivation. The current state that shapes the approach:

- The pipeline's stage order is fixed and already right for this change: `runOrganize` builds references from the output root (`buildFolderReferences(options.root, cache)`, `src/organize/pipeline.cpp:373`), routes (`routeItems`, `:374` — `routeConfident` `:54-101` files an image by character tag through `fileByCharacter` `:33-47`, which asks `owningFolder` `:38` first, or sends it to `mixed/`, and returns the indices that stay unassigned), then clusters the remainder (`clusterRemainder` `:283` → `clusterPending`, then `assignFolderMatches` `:298`, defined at `:227`).
- The routing already records where each filed image went, on the item itself: `fileByCharacter` (`src/organize/pipeline.cpp:33-47`) sets `item.folderName` and `item.folderSource` — `FolderSource::FolderMatch` when an existing folder owns the tag (`:39-40`), `FolderSource::CharacterTag` otherwise (`:45-46`) — and `mixed/` and `uncategorized/` are recorded through the same fields (`:82`, `:193`). `clusterRemainder` already reads those names back for its `usedNames` bookkeeping (`:291-292`). What is missing is only that nothing turns them into references, which is what this change adds.
- Acceptance figures this change is measured against (offline, one labelled collection, with both companion changes in place): end-to-end pairwise co-clustering F1 over the whole 3840-image gallery is 0.739 without this mechanism, 0.793 with it at the pre-calibration default and 0.819 at the calibrated single value, against a measured ceiling of 0.854; on the 1316 images routing leaves unassigned the fused agglomeration reaches 0.820 at the pre-calibration default and 0.850 at the calibrated one. At the measured pairing the mechanism moves recall 0.646 → 0.752 while precision stays within 0.007 (0.863 → 0.869).
- A reference is a `FolderReference` (`src/organize/assign.h:82`): name, the mean of its members' normalized features, how many members contributed a feature, and its character-tag ownership evidence (`soleTagCounts`). `buildFolderReferences` (`src/organize/teach.cpp:47`) walks the output root, builds one reference per folder from cached analyses through `accumulateMember` (`teach.cpp:17`), and orders the result by folder name so equal scores resolve the same way every run.
- `assignFolderMatches` (`pipeline.cpp:227`) compares each cluster's profile against every reference at the identity similarity threshold (`kIdentityTau`, `src/organize/cluster.h:25`) and, on a match, files all members under the reference's name (`FolderSource::FolderMatch`).
- The companion change `fuse-tags-and-average-linkage` (in flight, not implemented) turns that comparison into the calibrated fused score and gives a reference a mean identity-tag vector; this change is written against that post-state and lands after it. Today the comparison uses features only.
- A recorded diagnosis this change retires: the archived identity-embedding change's acceptance record describes the first-run/second-run difference as teaching-driven and stable thereafter ("the grouping is stable once the folders exist, so the run-1-to-run-2 movement is teaching, not noise", `openspec/changes/archive/2026-09-28-identity-embedding-for-grouping/tasks.md:58`; its run 2 recorded 41 of 48 folders captured by folder match). That was accurate for that release and it describes exactly the behaviour this change moves into the first run.

## Goals / Non-Goals

**Goals:**
- A first run over a gallery files clusters into the folders that same run's character-tag routing created, instead of leaving them `unknown_*` until a later run.
- The on-disk folder keeps precedence wherever it exists: ownership, names and existing contents are untouched.
- Given the run's analyses, the outcome is deterministic and independent of filesystem iteration order.

**Non-Goals:**
- Any change to routing, thresholds, cluster naming, `mixed/`, the work-stem grouping, copy semantics, the cache format, the models, the download set or the CLI surface.
- Persisting the routing-derived references: they are re-derived from cached analyses on every run, so nothing new is stored or invalidated.
- References from `mixed/`, from `uncategorized/`, or from the unassigned clusters themselves — a cluster teaching itself in the same run would make the order of clustering decisions observable, and `mixed/` is not one character.

## Decisions

### D1: Select the routing destinations from the items; do not re-derive and do not re-carry them

After routing, the items whose `folderSource` is a character folder (`CharacterTag`, or `FolderMatch` when an existing folder owns the tag) and whose `folderName` is set are exactly this run's character-folder assignments, and they sit in the same `items` vector `clusterRemainder` already receives. `mixed/` and `uncategorized/` are excluded by their source, not by their names. Alternatives: extending `routeItems`' return value with the destinations (the data is already on the item, so that would be a second channel to keep in sync); re-reading the output folders after routing (they do not exist on a first run — the case being fixed); letting the clustering stage call `owningFolder` and re-apply the confidence tiers itself (duplicates the routing rules and drifts from them whenever they change).

### D2: Build a routing-derived reference through the same accumulation as a disk reference

Group the filed images by destination and fold their cached analyses through the same `accumulateMember` (`teach.cpp:17`) that folder scanning uses, so a reference means the same thing regardless of source. Alternatives: a parallel code path (two definitions of a reference profile, which drift); calling `buildReference` (`teach.cpp:27`) with a synthetic path (it reads the filesystem, while the evidence lives in the cache keyed by content hash).

### D3: Merge by folder name, with the on-disk reference winning

A disk reference and a routing-derived reference for the same folder name become one reference whose members are the union of the disk members' analyses and this run's filed images' analyses. When a disk folder owns the character tag under a *different* name (a folder the user renamed), the rename has already decided the destination: the ownership rule inside `fileByCharacter` (`pipeline.cpp:38`) routes those images into the renamed folder, so the routing-derived reference carries that name and merges with the folder's own reference rather than producing a second one. Alternatives: keeping both references (two profiles for one character, one of them stale, so a cluster can match the wrong one); letting the routing-derived reference replace the disk one (its evidence is thinner than the folder the user arranged and renamed).

### D4: Deterministic reference order

Disk references keep the folder-name order they have today, and routing-derived references are inserted in destination-name order, so a cluster matching several references at the same score lands in the same folder on every run. Alternative: insertion order, which would inherit the order routing happened to use.

### D5: Reuse the existing capture step, with no new knob

`assignFolderMatches` iterates every reference as it does today; the routing-derived ones merely join that list, and the same threshold decides. Alternatives: a second, more permissive pass over tag-derived destinations (an unmeasured second threshold, and the asymmetry between the two comparisons was measured as small but real — splitting a knob for it buys 0.001 F1); post-clustering assignment by the cluster's dominant character tag (a different mechanism with its own failure modes; the measured lever here is the profile match).

### D6: Nothing is persisted

The references live for the run; the cache keeps exactly the analyses it keeps today. Alternatives: caching the reference set (cheap to rebuild, and it would need invalidation rules for a rename, a delete or a confidence change).

## Risks / Trade-offs

- **A mis-routed image can now attract a cluster into that character's folder** → measured at the calibrated pairing: recall moves 0.646 → 0.752 while precision stays within 0.007 (0.863 → 0.869); the capture still has to reach the threshold, and the ownership rule is unchanged.
- **A routing-derived reference can be thin evidence** (a folder created from a single image) → it is compared at the same threshold as any folder, so it captures only what is genuinely close to it; the user's rename still takes precedence on the next run.
- **Two runs of the same gallery can differ** because GPU inference is not bit-reproducible (a limitation already recorded by the archived identity change) → the requirement pins determinism *given* the run's analyses; a cluster sitting exactly on the threshold may flip, which is true of every threshold in the pipeline.
- **The first run's folder set changes** (a breaking output change) → documented in the proposal; nothing is moved or deleted, every image is still copied once, and the second run is as stable as it is today.
- **Cross-change coordination**: the teaching requirement is already being modified by the in-flight fusion change, so this change adds a *new* requirement beside it instead of editing it; when the two are archived, the fusion change's MODIFIED block and this ADDED block must be merged rather than one replacing the other.

## Migration Plan

None: no persisted state, no configuration, no new files, no change to what is downloaded. The behaviour appears in the first run after the upgrade — clusters that used to stay `unknown_*` until a second run now land in the character folder the same run created — and nothing on disk needs migrating in either direction. Rollback is reverting the commit.
