# Design: organize-existing-structure

## Context

The organize pipeline assumes a virgin directory: `scanImages` collects loose images, teaching (`buildFolderReferences`) reads only the children of `organized/`, and every destination lands under the output tree. The user's real directories hold their structure at the first level — per-character folders built or renamed by hand — which today is invisible (non-recursive) or destroyed (`-r` ingests it). Everything this change needs already exists as machinery: profile accumulation (`accumulateMember`), folder matching (`assignFolderMatches`), tag ownership (`owningFolder`), same-name reference merging (`buildSameRunReferences`), lazy destination creation (`executeOrganize` creates a folder only for an item being copied), hash-keyed caching, and the clustering entry point for demotion. The design is therefore mostly about deciding dispositions early and feeding the existing machinery new sources.

## Goals / Non-Goals

Goals: enter incremental mode without a flag when first-level folders exist; classify first-level folders (reference / input / ignored) with flags beating the name list beating the default; sample reference folders into profiles through the existing cache; mirror reference names as lazy skeletons under the output tree; make every disposition visible in the report.

Non-goals: writing into first-level folders; moving or deleting originals; auto-ingesting demoted folders; refreshing a folder's profile when its contents change (the hash-ordered sample is fixed per run — a changed folder re-samples naturally only for members that left the sample window); matching across simplified/traditional conversions; mirroring nested folder structure (only first level).

## Decisions

### D1: One disposition pre-pass, one module

A single pass over the target's first-level directories produces the whole `FolderDisposition` list (name, disposition, reason: default/list/flag) before any scan, in a new small module `src/organize/disposition.{h,cpp}`. Dot-prefixed first-level folders default to ignored (tool directories, not galleries); `--ingest` overrides. Every consumer — scan input assembly, reference construction, notices, validation, the report — reads this one list instead of re-deriving rules. Alternative rejected: spreading the rules across `scan.cpp` and `teach.cpp` (the misc-list predicate would be needed by both, and the notices/report would re-walk the directory).

### D2: The miscellaneous-name list is an exact-match constant

`kMiscFolderNames` lives beside the disposition code as a fixed array, lowercase for ASCII entries, with a comment naming its languages and matching rule. Match = whole trimmed name, ASCII folded to lowercase, other scripts exact, never substring, never cross-script conversion. Full list (additions are a constant edit, not a format change):

| Script | Names |
| --- | --- |
| English | `mix` `mixed` `misc` `miscellaneous` `assorted` `random` `various` `other` `others` `unsorted` `unclassified` `uncategorized` `unfiled` `pending` `todo` `inbox` `leftover` |
| Simplified Chinese | `杂项` `杂图` `混合` `未分类` `未整理` `未归类` `待分类` `待整理` `其他` `其它` `临时` `暂存` `新图` `下载` |
| Traditional Chinese | `雜項` `雜圖` `混合` `未分類` `未整理` `未歸類` `待分類` `待整理` `其他` `其它` `臨時` `暫存` `新圖` `下載` |
| Japanese | `その他` `雑多` `未分類` `未整理` `混合` `仮置き` `とりあえず` `一時` `新着` `ミックス` |

Deliberately excluded as too aggressive: `new`, `downloads`, `新下载` — a folder the user means as a deliberate staging area must not be silently reorganized; `--ingest` is the explicit path for those.

### D3: Reference sampling rides the existing stage machinery, with its own bar

For each reference folder: collect members recursively via the existing extension scan, hash each (cache key and ordering in one step), sort by content hash, take the first `kReferenceSampleSize = 20` analyzable members, analyze the ones the cache does not already hold, and `cache.put` them. The stage is a second `mediaitem::runStage` pass over these member items with its own `addBar("Sampling")` — same TTY-only rules, same cancel semantics, flushed through the same cache batching. Rationale for a separate stage rather than appending members to the loose-image list: the two populations have different fates (members never route, never copy; they exist only to build profiles), and a separate bar narrates the mode's one new cost honestly. Determinism is by content hash, the same order `buildSameRunReferences` already uses; a re-run samples the same members and pays nothing. The sampled analyses are ordinary cache entries keyed by content hash — no format change, no version bump, and a member that later migrates anywhere else stays cached.

Sample size 20 is a design constant with a calibration note: enough to give the mean feature and the sole-tag majority a stable vote at folder scale, small enough that 30 reference folders cost ≈600 inferences once. Tune by editing the constant, not the mechanism.

### D4: Demotion = cluster the sample, reuse the clustering entry point

After sampling, a reference folder's analyzable members (those with an identity feature) are handed to the existing clustering routine with the run's `identityTau`. Two or more resulting clusters ⇒ demoted: excluded from the reference list entirely, which removes both folder matching and tag ownership in one cut (`owningFolder` reads the reference list, so exclusion is sufficient). Demotion never flips disposition to input. Alternative rejected — a mean-pairwise-distance threshold test: that invents a second, uncalibrated threshold where the clustering operating point is already calibrated. False demotion (a varied single-character folder splitting at 20-sample scale) is fail-safe: the folder captures nothing and loose images fall to `unknown_*`; the report names the demotion, and `--identity-tau` or a larger sample is the tuning path.

### D5: Reference construction widens, then merges by name

`buildFolderReferences` gains a second source: first-level reference folders, their members read from the cache the sampling stage just filled — same `accumulateMember` fold, same skip rules (no analyzable member ⇒ not a reference; no cached feature ⇒ not a folder-match reference). A first-level folder and an output-root folder of the same name become one reference with the union membership, using the same by-name merge `buildSameRunReferences` performs for routing-derived references. Demoted folders are dropped before the list is built, so neither matching path ever sees them.

### D6: Scan input is assembled from the disposition list

`scanImages` takes the dispositions. A non-recursive run with first-level folders (incremental) collects the root's loose images plus each input-disposition folder recursively; reference and ignored folders are never entered. A recursive run disposes every first-level folder not named by `--ignore-folder` as input — the whole tree is scanned exactly as before this change, which is what keeps nested no-structure galleries working under `-r`. A non-recursive run without first-level folders scans loose images as today. The output-tree exclusion stays as is. Flag validation (name is an existing first-level directory besides the output tree; not given to both flags) runs in the command layer before any scan, through the existing argument-error channel.

### D7: Skeleton laziness and collision seeding are near-free

`executeOrganize` creates a destination folder just before copying an item into it, so laziness is nearly there — the one wart is that a failed copy leaves the created folder behind empty, which the lazy-creation requirement forbids. Reorder: stage the copy first, create the destination folder once the staging copy succeeded, then rename — a failed copy leaves no empty folder, and `createdFolders` counts only folders that exist. The other real gap is naming: `clusterRemainder` seeds `usedNames` from item destinations only, so an `unknown_*` name could equal an existing first-level folder's name. Seed `usedNames` with all first-level folder names (any disposition) and output-root folder names; the existing `assignUniqueFolderName` suffixing does the rest.

### D8: The disposition pre-pass and its notice live in the command layer

The disposition walk needs only the directory listing and the flags, so it runs in the command layer before the engines are built, and the one notice line naming incremental mode and the reference count prints there — before the loading spinner. Recursive runs print no new notice (their behavior is unchanged). Everything follows `console-output-conventions` and appears in the render-debug dump for layout checks.

### D9: The report gains dispositions, derived from the pre-pass

`ReportData` carries the disposition list; the report prints a disposition summary (reference / input / ignored / demoted, demoted rows carrying the `--ingest` hint) and adds zero-count rows for reference folders that matched nothing, labelled with their disposition as the assignment source. Zero-match rows are the set difference of reference names against destination folder names — no new bookkeeping in routing.

## Risks / Trade-offs

- [A varied single-character folder falsely demotes] → fail-safe outcome (no capture, `unknown_*` fallback); report names it; tune via `--identity-tau` or the sample constant.
- [A dominant-character mix folder wins tag ownership because demotion missed it] → demotion uses the same calibrated clustering as routing; dispositions are all visible in the report and dry-run; `--ignore-folder` is the escape.
- [First-run sampling cost surprises on many folders] → capped at the sample constant per folder, cached forever after, narrated by its own progress bar; `--ignore-folder` trims the reference set.
- [Cache grows with sampled entries] → bounded by sample × folders; same store and format, so rollback needs no cleanup.
- [Non-recursive destinations change for folders-bearing directories] → that old behavior is the parallel-structure failure this change removes; nothing is deleted or renamed, and `-r` keeps whole-set behavior as the escape.

## Migration Plan

Additive; no persisted format changes (cache format version unchanged), so deployment is the new binary and rollback is the previous binary. First incremental runs pay sampling once; nothing else changes on disk.
