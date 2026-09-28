## Context

See `proposal.md` — Why for the measured margin. Current state that shapes the approach:

- Both places that answer "same character" call the same cosine over unit identity features: `cosineSimilarity` and `accumulateFeature` (`src/organize/cluster.cpp`), consumed by the greedy `clusterPending` (which sorts its input by content hash, so the outcome depends on input order that is arbitrary for the user) and by `assignFolderMatches` (`src/organize/pipeline.cpp:227-251`), which compares a cluster's centroid against a reference folder's mean feature built in `src/organize/teach.cpp`. One constant, `kIdentityTau = 0.643050` (`src/organize/cluster.h:25`), governs both.
- Naming is already tag-derived and already has the helper the fusion needs: `clusterFolderName` ranks members' identity-bearing general tags through `isIdentityTag` at `kNamingConfidenceFloor = 0.55` (`src/organize/cluster.cpp`). The cache stores general tags at exactly that floor — "general at or above the naming floor" (`openspec/specs/image-character-organize/spec.md`, Cache and resume) — so the tag side of the fusion is available from the existing cache with no storage change, and no second model pass: the tagger already runs on every image.
- The previous change deleted the tag-vector machinery (corpus traits, idf, trait bands, top-K, vector floor, map-based cosine) and replaced it with the identity embedding (`openspec/changes/archive/2026-09-28-identity-embedding-for-grouping/proposal.md:13`). The evaluation that chose the replacement compared the two signals as *alternatives*, and a tag-derived embedding measured weaker in that role; combination was not measured then, and it is a different question from replacement, which is why the conclusion there does not settle this change.
- Two recorded diagnoses are re-read here rather than silently inherited: the archived change recorded the risk that one threshold would serve two different comparisons, image-versus-mean and mean-versus-mean (`openspec/changes/archive/2026-09-28-identity-embedding-for-grouping/design.md:89`), which measurement now answers with numbers: the two comparison kinds do not share one optimum, yet the best split pairing beats the best single value by 0.001 F1 end to end (D7), and the fused curve is flat across 0.62-0.72 — and its threshold rationale, the arithmetic conversion of the model's published metric threshold (that design's D3, `:46`), stops describing the shipped default once the score is fused, which is why this change restates the constant's meaning and recipe.

Calibration basis (all on features produced by the corrected identity preprocessing, one labelled collection of 3840 images across 46 per-character folders; pair-level metrics count same-character pairs among 7.37M pairs, and clustering metrics are reported on two populations — the protocol that replays the decision rule over all images as if unknown, and the populations the pipeline actually governs, i.e. the 1316 images the shipped tag routing leaves unassigned and the end-to-end figure over all 3840 images with routing applied):

- Pair level, best F1 per signal: identity feature 0.751 at cosine 0.75; identity tags with plain confidence weights 0.666; identity tags with idf weights 0.583; identity feature plus plain tags at 0.8/0.2 **0.806**.
- Clustering, best F1 per rule: average linkage on the 0.8/0.2 fusion with plain confidence weights **0.860** (0.852 with idf weights, the variant D1 rejects); average linkage on features alone 0.797; the greedy pass on features alone 0.695; a cluster-level centroid merge pass 0.705; kNN-vote join 0.733; single linkage 0.562; complete linkage 0.652; at a permissive threshold a merge pass and single linkage both collapse into chaining (F1 0.05).
- Pipeline populations (same collection, shipped tag routing: 2443 images placed by character tag, 81 in `mixed/`, 1316 left unassigned). On the 1316 unassigned images the shipped rule scores 0.684, corrected preprocessing alone 0.713, and corrected preprocessing with this change's fused score and agglomeration 0.820 at one knob of 0.66 (0.850 at its optimum of 0.70). End to end over all 3840 images the same three score 0.697, 0.707 and 0.739; adding a same-run folder match reaches 0.793 with one knob at 0.66 and 0.819 at 0.70. The measured ceiling — every unassigned image joining its character's existing folder — is 0.854, so most of the remaining headroom is not a clustering-parameter problem.
- Threshold plateau of the fused score, on the all-images-as-unknown protocol: F1 at or above 0.83 for every threshold from 0.62 to 0.72 (0.827 at 0.60), peak 0.66 with precision 0.907 and recall 0.817; on the pipeline's own populations the best single value is 0.70, which is why the shipped default sits where both are jointly strongest rather than at either peak. Features alone peak at 0.74 with a narrow window (0.70 → 0.757, 0.74 → 0.797, 0.76 → 0.775) and fall away quickly below it (0.60 → 0.624).
- Order sensitivity of the shipped greedy pass, measured on the pipeline's own population over five shuffles of the same images: F1 spans 0.085 on the shipped raw features and 0.080 on the corrected ones, where hash order scores 0.684 and 0.713 — so the shipped input order is a favourable draw rather than a neutral one. On the all-images-as-unknown protocol the same measurement spans 0.512-0.550 at the shipped threshold.
- Structural limit, measured and not addressed here: within-character cosine p05 is 0.545 and between-character p95 is 0.626, so a fraction of a character's artwork is closer to another character than to its own mean; no threshold separates them, and "characters split across clusters" stays well above zero at every threshold (87-100%). The fused score raises precision and consistency, not separability — consolidation still comes from teaching and from the user's names.

## Goals / Non-Goals

**Goals:**
- Raise grouping quality using only the signals, models and files that are already in place, and record the calibration so the numbers can be re-derived rather than trusted.
- Make the clustering outcome independent of the order images happen to arrive in.
- Turn the threshold into a calibrated operating point that a user can move per gallery, with the meaning and the recipe written down.

**Non-Goals:**
- Any new model, dependency or download; the tagger and identity model stay as they are.
- Crop or flip test-time augmentation, a different identity model, and learning a projection head from renamed folders (recorded as follow-ups, each needs its own measurement).
- DBSCAN/OPTICS on the similarity graph, hubness correction and reciprocal-kNN filtering. The first is the model authors' own recipe but adds a dependency and was not measured here; the second needs a k-occurrence skewness measurement that has not been run. Agglomeration already captures the mechanism this change is after (order-independent merging) with no new dependency.
- Eliminating fragmentation, and changing naming, routing, `mixed/` handling, the work-stem grouping or the copy semantics.
- Tuning the signal weight per run (α is fixed; see D2).

## Decisions

### D1: The tag side is a confidence-weighted unit vector over identity-bearing general tags, with no corpus statistics

Measured: plain confidence weighting scores 0.666 pair F1 against 0.583 for idf weighting, so the statistic-heavy part of the deleted machinery stays deleted, and the fusion needs no corpus-wide fit (the requirement forbids one). Alternatives: idf/tf-idf weighting (worse and reintroduces a fitting step), the tagger's character head as a third term (measured +0.002 F1 at best, and it mixes a second vocabulary and its own threshold into the score), raw tag counts (ignores confidence).

### D2: Fixed weights, 0.8 on the identity feature and 0.2 on the tag evidence

The measured difference between 0.7/0.3 and 0.8/0.2 is 0.008 F1 in opposite directions on different metrics, i.e. inside noise. Exposing α would create a second knob whose effect interacts with the threshold, and the threshold is the knob users can reason about. Alternatives: learn the weight (no labels at run time; a per-user fit is the projection-head follow-up, not this), expose α (rejected above).

### D3: Combine the two signals at the score level, not by concatenating vectors

The combined score is `0.8·cos(feature) + 0.2·cos(tags)`. Concatenating scaled vectors into one higher-dimensional vector and taking its cosine weights contributions by magnitude, not by the stated weights, so the measured table would not describe the shipped behaviour. Alternatives: concatenate and re-normalize (same objection), average the raw vectors elementwise (undefined for different widths).

### D4: Cluster by average linkage over the pairwise combined scores

Repeatedly merge the pair of clusters whose *mean cross-pair* similarity is highest, while that mean reaches the threshold; stop when no pair does. Ties are broken deterministically — the pair whose clusters were created earlier goes first — so the same input always produces the same partition. This is what removes the order dependence (the shipped pass merges into the first cluster that is close enough, in hash order) and it measured best among the rules tried: 0.860 as a fused score against 0.695 for the greedy pass on features alone. Alternatives and their measurements: single linkage (chaining; 0.562 alone, collapsing to 0.05 at a permissive threshold), complete linkage (precision 0.99 with recall 0.50; 0.652), a cluster-level centroid merge pass (0.705, and it chains transitively at permissive thresholds), kNN-vote joining (0.733), DBSCAN/OPTICS (deferred, see Non-Goals). Acceptance of the cost: the implementation needs the condensed pairwise matrix, which is why D5 exists.

### D5: A recorded image-count ceiling with a logged fallback

The condensed matrix is 8 bytes per pair: about 100 MB at 5k images (the observed upper bound for a single run that goes through the calibration) and about 400 MB at 10k, which is where the ceiling sits. Above a recorded ceiling (10,000 analysed images) the run falls back to the previous greedy pass and says so in its output, so a large gallery degrades in quality instead of failing or exhausting memory — no silent behaviour change. Alternatives: a hard error (breaks a run that used to work), blocked or streaming linkage (engineering not justified by the observed workload), relying on the operating system to swap.

### D6: Two calibrated defaults, one per scoring mode

0.70 for the combined score, 0.74 for a comparison that has no tag side (an image with no identity-bearing tag above the floor, or a platform whose tagger is unavailable). A fused score and a bare cosine are different coordinates — the same images peak at 0.70 and at 0.74 — so one number cannot serve both, and the shipped default sits where the two comparisons are jointly strongest rather than at either peak. Alternatives: a single number (mis-calibrated for the tag-less path, which is also the non-Windows degradation path), requiring tag evidence for clustering (would drop images that clustering currently handles).

### D7: One threshold knob for both comparison kinds, in the CLI and the config file

The two comparison kinds do not share a single optimum: with folder match in play, fused clustering at 0.74 paired with folder match at 0.643 reaches 0.8200 end to end while the best single value (0.72) reaches 0.8187 — so splitting the knob would buy 0.001 F1 for a second dimension nobody can calibrate by hand, and the one-knob property survives on measurement rather than convenience. The knob is `--identity-tau`, validated to (0, 1], with the standard CLI > config > default precedence; out-of-range values fail as an argument error before any scan. Alternatives: separate knobs per comparison kind (measured above), a knob for α (D2). The set of keys the config file accepts is fixed by the `user-config` capability, so this option's config key comes with a delta for that capability.

### D8: The threshold's meaning is restated, and the calibration recipe recorded

The constant's comment keeps the model's published conversion as a reference point for the feature cosine (0.178475 difference equals cosine 0.643050) and states what the shipped defaults are, on which kind of collection they were measured, what the plateau looks like, and that a model, preprocessing or weight change calls for re-measurement. Parameterised thresholds have no derivation to inherit, so they need a recipe. Alternatives: deriving the default arithmetically (it described a score that no longer exists), no comment (the next person cannot re-calibrate).

### D9: No cache change

The tag side is built from tags the cache already stores at exactly the floor it uses (the naming floor, 0.55), and the tag vector is derived rather than stored, so neither the entry shape nor its stored floors change; the calibrated default is not persisted either. The identity input change that does invalidate the cache belongs to the companion change, whose requirement already covers a meaning change.

## Risks / Trade-offs

- **Fragmentation stays**: a character's varied artwork is measurably closer to other characters than to its own mean, so the same character can still land in several folders. → Stated, not hidden: the change raises precision, recall and consistency, and consolidation continues to come from teaching plus the user's names; a model-level fix is a separate follow-up.
- **The calibration comes from one collection** of AI-generated art of visually similar characters. → The default sits inside a wide plateau (0.62-0.72 all at or above 0.83 F1), the implementation re-measures on a second, differently labelled collection, and the knob exists precisely because one default cannot fit every gallery.
- **The fused score can merge two characters that share appearance tags**, just as the feature can merge two whose artwork is similar. → The measured precision at the plateau is 0.87-0.99 and the F1 is above either signal's alone; the tag side carries the lower weight; the user can raise the threshold.
- **The tagger's vocabulary is part of the score now**, so a tagger swap changes grouping. → The tagger files are pinned with sizes and checksums, and the companion change's cache requirement makes a model change raise the cache version, so stale tag pairs cannot be compared against fresh ones.
- **`unknown_*` folders and their membership change for existing galleries** — a breaking output change for anyone who has arranged folders by hand. → Documented in the proposal; nothing is moved or deleted; renamed folders keep teaching; the run reports folders as it does today.
- **Average linkage ties** could make the outcome depend on iteration order. → The deterministic tie-break rule in D4 is pinned by a test that runs the same input twice and across a shuffled input order.
- **O(n²) memory and time** at the ceiling. → Ceiling plus logged fallback (D5), and a verification step measures a 5k-image run's wall clock and peak memory.

## Migration Plan

None: no new files, no required configuration (the new config key is optional), no persisted-state change, and no change to what is downloaded. The first run after the upgrade regroups the gallery under the new score, so `unknown_*` folders can differ in membership and name from the previous run; nothing is moved or deleted, and an existing renamed folder keeps teaching through the new score. Rollback is reverting the commit.
