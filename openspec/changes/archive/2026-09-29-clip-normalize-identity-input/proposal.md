## Why

The identity model is fed raw 0..1 pixels, but its upstream reference implementation scales to 0..1 and *then* normalizes with the model's documented channel mean and standard deviation — and it resizes with a bilinear filter where encro's ffmpeg graph uses the filter's bicubic default. So encro's embeddings are computed from inputs the model was not trained to receive, and the published operating point the identity threshold is derived from does not apply to them. Measured on a labelled 46-character / 3840-image collection with the shipped decision rule, restoring the upstream preprocessing raises pair-level F1 at the shipped threshold from 0.480 to 0.580 and clustering F1 from 0.561 to 0.584, and moves the clustering protocol's achievable F1 from 0.675 to 0.727 at its own best threshold and the pair-level best from 0.686 to 0.751 (same images, same graph, only the input preparation differs). The normalization carries most of that gain and the resampling filter contributes about 0.01 F1 — but the filter has to match for the input to be the reference's input at all: under bicubic only 638 of 3840 images agree with a reference extraction to a cosine of 0.999, against all of them once the filter matches. On the populations the pipeline actually governs the same fix moves clustering F1 from 0.684 to 0.713 on the images routing leaves unassigned, and from 0.697 to 0.707 end to end over the gallery.

## What Changes

- The identity path SHALL prepare the model input the way the model's reference implementation does: stretch-resize to 384x384, scale to 0..1, then subtract the documented channel mean and divide by the documented channel standard deviation — inside the existing byte-to-float conversion, so no extra decode pass and no new model file.
- **BREAKING** for caches: stored identity features change meaning, so the analysis cache format version is raised and an existing cache is discarded — one full re-analysis per gallery after the upgrade, no other user-visible step.
- The input contract becomes a specified requirement of the capability, so a future model swap or preprocessing edit cannot drift from its reference silently again, which is exactly how this defect survived.
- Deliberately out of scope: the identity similarity threshold default stays at the published-derived 0.643050 here. Re-calibration belongs to the follow-up grouping change, which fuses a second signal; the published conversion does not apply to a fused score.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: adds the identity model's input-preparation requirement, and requires the cache format version to be raised whenever the *meaning* of stored analysis changes (a model or preprocessing change), not only when the entry shape changes.

## Impact

- `src/tagger/mapping.{h,cpp}` — the identity input conversion gains the channel normalization; this is the whole behavioural change.
- `src/tagger/preprocess.cpp` — the identity filter graph gains the reference's resampling filter (`scale=384:384:flags=bilinear`); the tagger path and its white-canvas contract are untouched. `tests/tagger/preprocess_tests.cpp` pins that graph and gains the filter assertion (task 2.4).
- `src/tagger/model_store.cpp` — the identity pin's comment gains the input contract it belongs to, so the pin and its contract sit together.
- `src/organize/cache.cpp` — cache format version raised to invalidate entries computed from the old input.
- `tests/tagger/mapping_tests.cpp` — the existing identity-input case is rewritten to the normalized contract (it pins the raw 0..1 scaling today, so it must change with the code); `tests/organize/stage_tests.cpp` — pins the version rule; `tests/tagger/real_model_tests.cpp` — the real-model smoke keeps working without a pinned threshold.
- Existing installs: one re-analysis per gallery; CLI surface, model files and pinned checksums are unchanged, so no new download.
- The archived identity-embedding change recorded that the model's published threshold landed on the F1 optimum of that evaluation; that observation was measured on the raw input, so it describes the old preprocessing and is superseded — the follow-up change re-calibrates on the corrected features.
