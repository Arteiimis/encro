## Why

`encro organize` decides "same character" from Danbooru *tag* vectors, which are a lossy, hand-filtered projection of the model's own features: 10 tags capped by identity substrings, weighted by corpus document frequency, restricted to a trait band. Measured on two real galleries (1300 and 1206 images), the current signal leaves same-character images in the same folder only 37% / 18% of the time and splits 64% / 82% of characters across several folders.

A purpose-trained anime same-character model (CCIP) reaches 66% / 29% co-cluster recall and cuts the split share to 57% / 36% on the same images at its published threshold, and needs no hand-maintained tag list, no corpus statistics and no per-corpus weight tuning. Its metric is plain cosine similarity, so the pipeline can use it in-process.

The two places that already judge "same character" also disagree today: clustering accepts at cosine 0.50 while teaching folder-matching requires 0.65 over the same vector space.

## What Changes

- **BREAKING**: `--download-models` gains a second pinned model — CCIP `ccip-caformer-24-randaug-pruned` feature extractor (150 248 245 bytes, 143 MiB) — and the model directory must contain it before any `organize` run. An existing `~/.encro/models` needs one `--download-models` run; its download message and the flag's help text grow from ~400 MB to ~545 MB. The tagger stays required.
- Identity similarity switches from appearance-tag vectors to the model's 768-float identity feature, L2-normalized per image: **one** cosine threshold replaces both `kClusterTau` (0.50) and `kFolderTau` (0.65) for clustering acceptance and teaching folder-matching alike, at the cosine equivalent of CCIP's published difference threshold (0.178475 → 0.643050).
- The sparse-vector machinery is deleted: `CorpusTraits` (idf weights, document frequency band), the top-K tag cap, the vector evidence floor and the map-based cosine. The identity-tag patterns and blocklist survive only as the source of `unknown_<tags>` folder names, which stay tag-derived because the embedding has no vocabulary.
- The analysis cache stores the identity feature per image and its version is bumped, so a cache written by an earlier version is discarded instead of being read as featureless.
- The tagger remains required and unchanged in role: character-tag routing, multi-subject `mixed/` detection and folder naming.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `image-character-organize`: appearance clustering for unassigned images (input signal, threshold, naming source), teaching by renamed folders (similarity signal and threshold), cache and resume (stored analysis shape and version), model and runtime file management (a second pinned model file).

## Impact

- `src/organize/`: `cluster.{h,cpp}` (feature vectors, one threshold, naming), `teach.{h,cpp}` and `pipeline.cpp` (folder-match similarity, dropped corpus traits), `cache.{h,cpp}` (feature persistence, version 2), `organize_types.h` (analysis = tags + identity feature) with `assign.{h,cpp}`, which reads the tag arrays through that type, `organize_command.cpp` (second engine, required-model check).
- `src/tagger/`: `model_store.cpp` (pinned CCIP file), `engine_factory.{h,cpp}` (a feature-extraction engine beside the tagger, with the same unsupported-platform degradation), `preprocess.{h,cpp}` (a 384×384 stretched RGB variant — the existing one is 448×448 white-padded), plus the new engine that binds the extractor's `[batch,3,384,384]` input and `[batch,768]` output.
- `src/cmd/` and `README.md`: the `--download-models` help copy and the model-size note (`README.md:179`).
- Tests: `tests/organize/` (clustering, teaching, pipeline, cache, stage), `tests/tagger/` (preprocessing variant, model store pinning), `tests/e2e/encro_organize_tests.cpp` (its fixture writer); the `ENCRO_FAKE_TAGGER` fixture grows a feature field so organize tests can drive identity similarity without a real model.
- Users: existing model directories need the new file before the next run; non-Windows builds keep today's degradation (no model, no clustering).
- Licensing: CCIP is OpenRAIL and downloaded at runtime, never vendored; encro stays MIT.
