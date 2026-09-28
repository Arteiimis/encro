## Context

See `proposal.md` — Why for the defect and the measured effect. The current state that shapes the fix:

- The identity input conversion is `toIdentityInput` (`src/tagger/mapping.cpp:50-62`): interleaved rgb24 bytes in, planar NCHW floats scaled by `1/255` out, nothing else. Its consumer is `OnnxFeatureEngine::extract` (`src/tagger/onnx_features.cpp:36` decode, `:40` conversion, `:41` tensor shape, `:69` unit-normalization of the output).
- The decode step is almost right: `specOf(InputKind::Identity)` (`src/tagger/preprocess.cpp:19-27`) renders `scale=384:384,format=rgb24` with no aspect-preserving pad and no white backdrop, and `kIdentityEdge`/`kIdentityBytes` (`src/tagger/preprocess.h:32-34`) pin the size — but an unqualified ffmpeg `scale` filter resamples with **bicubic** by default, while the model's reference resizes with **bilinear**. Measured on the same collection, same graph and same normalization: the per-image cosine between the two filters is p50 0.9982, p01 0.9935, min 0.9360, and only 638 of 3840 images reach 0.999, so they are not interchangeable and a comparison against a reference extraction (D3) is only meaningful once the filter matches. The reference's filter is also the slightly better feature set (pair best-F1 0.751 against 0.741, and 0.580 against 0.541 at the shipped threshold), so this change adopts it rather than documenting the difference away.
- What is missing is the step upstream performs after scaling. In `imgutils/metrics/ccip.py`, `_preprocess_image` is `resize(384,384, BILINEAR)` → `astype(float32)/255` → `_normalize(data)`, and `_normalize` is `(data - mean[:, None, None]) / std[:, None, None]` with `mean=(0.48145466, 0.4578275, 0.40821073)`, `std=(0.26862954, 0.26130258, 0.27577711)`. The same reference implementation is what produces the model repository's `metrics.json` threshold that the identity threshold is derived from.
- The tagger path is unaffected: its reference contract is raw 0..255 on a white 448x448 canvas (`toInputFloats`), already pinned by a test, and no normalization is added there.
- Recorded diagnosis that is now stale: the archived identity-embedding change states that the model's published threshold landed exactly on the F1 optimum of its evaluation (a threshold-adoption justification). That evaluation ran on the raw input, so the observation describes the old preprocessing, not the model; the corrected features move the optimum, and the follow-up grouping change re-calibrates deliberately.
- The card's comment on the cache version (`src/organize/cache.cpp:21-24`, `kVersion = 2`) explains the last bump as an entry-shape change. Meaning changes were never covered — which is how a feature-computation change could have shipped without invalidating stored features.

Measurements quoted here and in `proposal.md` come from one labelled collection of 3840 images in 48 per-character folders, which are 46 characters once the two characters that are each split across two folders are merged, both feature sets extracted from the same images with the same graph, differing only in input scaling; pair-level metrics count same-character pairs of all 7.37M pairs, and clustering metrics are reported both on the all-images-as-unknown protocol this calibration uses and on the pipeline's own populations (the images the shipped tag routing leaves unassigned, and the whole gallery end to end). Numbers are reproducible from the change's tasks (verification step 2) and are restated in this change's tasks rather than linked to a scratch path.

## Goals / Non-Goals

**Goals:**
- The identity feature is computed from input the model's reference implementation would accept, so the published operating point is meaningful for these features again.
- The input contract is recorded where the model is pinned and where the conversion happens, so a model swap cannot silently inherit the wrong preparation.
- Cache invalidation follows the meaning of the stored analysis, not only its shape.

**Non-Goals:**
- Re-calibrating the identity similarity threshold. The corrected features move the optimum, but this change deliberately keeps the current default; the follow-up grouping change replaces it with a calibrated value for a fused score.
- Changing anything about the decode other than the identity graph's resampling filter: the tagger's preprocessing, the pinned model files and the download logic are untouched, so there is no new download and no new pinned artifact.
- Making the normalization constants configurable. They are model metadata; a knob would let users silently break the contract.

## Decisions

### D1: Normalize inside the byte-to-float conversion, not in the filter graph

`toIdentityInput` gains the per-channel `(value - mean) / std` step, and the identity filter graph's scale gains the reference's resampling filter: `scale=384:384:flags=bilinear,format=rgb24`. The normalization stays in C++ rather than moving into the graph. Alternatives: leaving ffmpeg's default bicubic filter (measured above: not interchangeable with the reference — only 638 of 3840 images agree to 0.999 — and it is the weaker feature set, so D3's acceptance comparison could not pass); expressing the affine step as ffmpeg filters (`lut`, `colorchannelmixer`) puts model constants into a shell command line and depends on ffmpeg's colour handling and plane order, which the previous change already had to work around; a second ffmpeg pass costs a decode per image for arithmetic; an extra ONNX graph would add a second session and a second pinned file to the download path. The conversion loop already touches every byte exactly once per channel, so the affine step is free there.

### D2: The constants live with the conversion and are mirrored at the model pin

`mapping.h`/`mapping.cpp` name the pinned model file and the upstream function the constants come from, and the identity entry's comment in `modelFiles()` (`src/tagger/model_store.cpp`) states the same input contract in one line. Alternative: a shared constants header — premature for four numbers used at one site; the comment pairing is what makes a swap fail loudly enough for a human to notice.

### D3: Compute in float32, in the same order as upstream

Upstream computes on a float32 array. Keeping `float` and the same operation order (`value/255`, subtract channel mean, divide by channel standard deviation) lets the implementation be checked against a reference extraction of the same images: the acceptance step compares the two feature sets and requires agreement at the distribution level — at or above 0.999 for the first percentile and at or above 0.9999 for the median (measured 0.99956 and 0.99997), plus decision-level metrics within 0.005 of the reference's. A per-image floor was tried first and dropped after measuring it: 7 of 3838 images on the labelled collection sit between 0.9960 and 0.999 because ffmpeg's JPEG decoding and swscale bilinear resampling differ from libjpeg's and PIL's in the last bits, while both corpora's decision metrics agree with the reference to within 0.002. Exact equality is not assertable anyway — CUDA execution is not bit-reproducible on this host, a limitation already recorded by the previous change. The comparison is only meaningful because D1 makes both sides resample the same way: under the previous bicubic default just 638 of 3840 images cleared even that floor. Alternative: computing in double and storing floats — harmless numerically but it would blur the one-to-one comparison with the reference.

### D4: Raise the cache format version to 3

Every stored identity feature was computed from the old input, and clustering compares features against centroids, so a cache mixing both preparations would compare incomparable values. Alternatives: leaving the version alone and telling users to pass `--recluster` (silent wrong results for everyone who does not read release notes), or versioning only the feature field (more machinery for the same outcome). The version constant's comment is updated to state the new rule: the version changes when the *meaning* of stored analysis changes. Its model-replacement half remains a review-time rule — nothing links the pinned checksum to the version constant, so a swap has to raise it deliberately — and this sentence is where that instruction lives; a follow-up could derive one from the other, but nothing in this change needs that.

### D5: Do not re-calibrate the threshold in this change

With corrected features the shipped default 0.643 is no longer at the old optimum, but it is already an improvement over the old preprocessing at the same threshold (on the all-images-as-unknown protocol pair F1 0.480 → 0.580 and clustering F1 0.561 → 0.584; on the pipeline's own populations clustering F1 on the unassigned images moves 0.684 → 0.713 and the end-to-end figure 0.697 → 0.707), so there is no regression window to close. Re-calibrating here would mean picking a value that the follow-up change replaces with a fused-score value, and would couple two independent reviews. The interim state and its numbers are recorded in the proposal and tasks.

## Risks / Trade-offs

- **The corrected input leaves the shipped default threshold looser than the new optimum** — a lower threshold joins more, and at 0.643 the corrected features reach 0.584 against 0.727 at their own optimum of 0.755. → Deliberate, documented, and an improvement in both positions; the follow-up change owns re-calibration.
- **The quoted gains come from one collection** (AI-generated art of visually similar characters) and thresholds do not transfer between collections. → The change itself is not a threshold claim: it removes a divergence from the model's reference contract, which holds regardless of collection. Verification re-measures on a second, differently labelled collection before the commit.
- **GPU inference is not reproducible run to run**, so "features match the reference" can only be asserted with a tolerance. → Compare by cosine with a stated distribution floor (first percentile at 0.999, median at 0.9999) rather than bitwise equality, and keep the existing self-consistency smoke as the cheap guard.
- **Upstream could change its preprocessing.** → The contract is written next to the pin with its upstream source named, and the spec requires restating it whenever the model file is replaced; a swap review has one place to check.
- **Existing galleries pay one full re-analysis.** → Same cost profile as the previous cache bump, no user data is lost, and the copies under `organized/` are untouched; the run resumes from the new cache afterwards.

## Migration Plan

None: no persisted user data changes and no configuration is added. The first run after the upgrade re-analyzes every image once because the cache version differs; reruns are unaffected. Rollback is reverting the commit, which re-analyzes every image once in the other direction.
