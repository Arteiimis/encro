## Context

See `proposal.md` — Why. What shapes the approach:

- Two places judge "same character", both over tag vectors: clustering acceptance compares an image's vector against a cluster centroid (`clusterPending`, `src/organize/cluster.cpp:182`), and teaching folder-matching compares a cluster centroid against a reference folder's mean vector (`assignFolderMatches`, `src/organize/pipeline.cpp:226-229`). Both consume `normalizedAppearanceVector` (`cluster.cpp:170`), which exists only to build that vector from `CorpusTraits` (`cluster.cpp:94-135`: idf weights, the df trait band, the identity-tag filter, the top-K cap).
- One analysis pass today: `analyzeMissing` calls `engine.classify` once per image missing from the cache, and the cache (`src/organize/cache.cpp`, `kVersion = 1` at `:21`) stores the three tag arrays keyed by content hash. The cache is the only source for teaching references: `buildFolderReferences` (`src/organize/teach.cpp:70`) walks `organized/` and means its members' cached vectors, so anything the similarity needs must be *in the cache*.
- Model plumbing is one manifest: `modelFiles()` (`src/tagger/model_store.cpp:84`) drives both the presence check (`allFilesPresent` `:135`, called from `requireModels` in `organize_command.cpp`) and the download (`downloadMissing` `:264`), with `HF_ENDPOINT`/mirror fallback and per-file size+sha256 pinning. `makeTaggerEngine` (`engine_factory.cpp:114`) returns one engine and has two non-ONNX arms — `EnvFakeTagger` (`:30`, the `ENCRO_FAKE_TAGGER` fixture) and `UnsupportedPlatformTagger` (`:100`).
- Preprocessing is at fixed 448: `kInputEdge`/`kInputBytes` (`src/tagger/preprocess.h:19-21`) and one filter graph that letterboxes onto a white canvas (`preprocess.cpp:9-24`); the runner enforces the exact byte count (`:29-48`).
- Evidence for the swap is in the eval worktree (`../encro-model-eval/.eval/RESULTS.md`): on 1300- and 1206-image galleries the identity model beats the tag vector on every pair-level metric, lifting same-character co-clustering from 37%/18% to 66%/29% and cutting the share of characters split across folders from 64%/82% to 57%/36%. Its 366 MB sibling splits fewer characters on the first gallery (42.9% against 57.1%) yet is behind or level on recall, precision and the second gallery's identical 36.4%, so the 143 MB variant is the one adopted. The tagger's *own* embedding output loses to the tag vector it would replace (AUC 0.58-0.66 against 0.71-0.84), so the second model is a separate dependency, not a free second output.
- Verified against the model files themselves: the extractor takes `[batch, 3, 384, 384]` float and returns `[batch, 768]`, and its shipped metric head implements exactly `clip(0.5 * (1 - cosine), 0, 1)` — the same graph in both published variants, magnitude-invariant, reproducing the formula to 6.1e-08 over 200 random pairs (measured on the reference host during the evaluation, not recorded in a repo artifact). The file to pin is `deepghs/ccip_onnx` → `ccip-caformer-24-randaug-pruned/model_feat.onnx`: 150 248 245 bytes, sha256 `4ea118d16496274f4f6e08d3afc768cc592389e8f7f32f8732ce2215c228ac5f`, basename `model_feat.onnx` (no collision with the tagger's `model.onnx` in the flat model dir).

## Goals / Non-Goals

**Goals:**

- One notion of "same character" for clustering and teaching, with a threshold traceable to the model's own published calibration instead of to per-corpus fitting.
- Delete the machinery that exists only to shape the similarity signal, rather than feeding it a different vector through the same code.
- A second model that follows the existing pinned-download path exactly: one manifest, one presence check, one `--download-models` run.

**Non-Goals:**

- Changing routing: character-tag assignment, `mixed/` detection, folder ownership, `--min-confidence` semantics and exit codes are untouched.
- Changing `unknown_source_<work>` singleton grouping, copy semantics or report format.
- Judging multi-subject images with the identity model: the pending set is single-subject by construction and the model is documented for single-character images.
- Introducing a general "any ONNX model" engine abstraction: two engines with two small interfaces is the smaller change.
- Re-tuning the threshold from a corpus-wide fit: the corpus is what the model is supposed to replace.

## Decisions

### D1: The identity model runs beside the tagger; the tagger keeps every current job

The tagger stays required and unchanged in role — character-tag routing, multi-subject `mixed/` detection, folder naming — because the embedding carries no labels and no subject count. `mixed/` in particular cannot move: the identity model judges one character, so it has nothing to say about an image with two.

Rejected: replacing the tagger entirely with the identity model (no vocabulary means no `mixed/`, no folder names, no ownership) and using the tagger's own ONNX embedding instead of a second model (measured worse than the tag vector it would replace).

### D2: The identity feature belongs to the analysis, not to the tagger's output type

`AnalysisResult` (`src/organize/organize_types.h:34`) stops being an alias for `tagger::TaggerOutput` and becomes `{tagger::TaggerOutput tags; std::vector<float> identity;}`. Call sites read `.tags.general` etc.

The alternative — adding an `identity` field to `tagger::TaggerOutput` — costs zero call-site churn but makes the tagger module the owner of an organize-layer product, and leaves the type carrying a field no tagger produces (the tagger's own embedding output is a *different* vector, evaluated and rejected in D1). The alternative of a second cache file for features is worse: two stores for one analysis pass, two version/discard paths, and `buildFolderReferences` would have to read both.

### D3: The similarity is computed in-process; the shipped metric head is neither pinned nor run

The head is a fixed decreasing function of the cosine — `clip(0.5 * (1 - cosine), 0, 1)` — identical in both variants and independent of vector magnitude. Feeding features to it would mean a second ONNX session per comparison (or an N×N matrix for a function of one cosine) to obtain what three arithmetic operations produce, and pinning it would add a 1.6 KB file whose content carries no calibration.

The threshold is therefore expressed in cosine terms: the published difference 0.178475 equals cosine `1 - 2 × 0.178475 = 0.643050` (`0.573538` for the larger variant, not adopted). The eval harness reached its numbers *through* the head, so this change reproduces the measured operating point exactly rather than approximating it.

Rejected: pinning and running the head "for fidelity" (the fidelity is the formula, verified); shipping the head's scale as a constant to be applied at run time (that is what D3 does, but as a threshold, which is the only value the pipeline compares against).

### D4: One threshold serves clustering and teaching

Today the same tag-vector space is read at 0.50 for clustering (`cluster.h:21`) and 0.65 for folder-matching (`cluster.h:25`) — two numbers that answer the same question and disagree.

One constant, `kIdentityTau = 0.643050`, answers it once. The genuine asymmetry is that teaching compares two *means* (cluster centroid vs reference mean) while clustering compares an image against a mean; a mean-vs-mean similarity runs systematically higher, so the same threshold is stricter for teaching than a loose one would be. That direction is the safe one for a forced assignment into a user-named folder.

Rejected: keeping two constants at the same initial value (a second constant nothing measures yet, kept in sync by hand), and carrying the old 0.65 across (it was calibrated against tag-vector ranges, not this model's — the observed cosine-equivalent published threshold is the only measured operating point available until the acceptance run).

### D5: Cluster names stay tag-derived; the corpus statistics are deleted

The embedding has no vocabulary, so `unknown_<tags>` names (`clusterFolderName`, `cluster.cpp:223`) are read from the cluster members' own general identity tags. Ranking becomes member frequency, ties broken by mean confidence, then tag text: within a cluster, the tags most members share are what describes it, and this needs no corpus-wide fitting — which is the point of the deletion. The identity patterns and the blocklist stay; they select which tags may name a folder.

Deleted with `CorpusTraits`: `buildCorpusTraits`, `inTraitBand`, the `kTrait*` constants, `kTopKTags`, `appearanceVector`, `normalizedAppearanceVector`, and the map-based `cosineSimilarity`. `kVectorFloor` does not vanish with them — it is the *storage* floor the cache applies to general tags (`cache.cpp:142`), and it survives under a name that says what it now serves, `kNamingConfidenceFloor = 0.55` (today's value, unchanged), because naming is the general tags' only remaining consumer. The `traits` parameter disappears from `clusterPending`, `buildFolderReferences` and `clusterRemainder`, and `pipeline.cpp:364` no longer builds them.

Rejected: keeping idf weighting for names only (it existed to shape the similarity vector; for naming one cluster, a corpus statistic is the fitting this change removes) and index names like `unknown_01` (loses the description the spec promises and the rename workflow builds on).

### D6: The cache stores the feature under content hash, at format version 2

Entry shape grows one field: `{"general": [[tag, confidence], ...], "character": [...], "rating": [...], "identity": [f0, f1, ... 768 floats]}`. The feature is stored normalized (unit length) so a stored vector can be averaged into a centroid directly, exactly as the tag vectors were.

Floats are written as plain JSON numbers: `boost::json` emits the shortest representation that round-trips the double, and a float32 value is exactly representable as a double, so the round trip is lossless without an encoder. Size goes from roughly 0.6 KB to roughly 10 KB per image (1300 images: ~12 MB); with the existing 64-put flush batching that is tens of megabytes of rewrite per run on the reference gallery and about 15 s of I/O at 10 000 images — acceptable, and a base64 blob field or an append-only sidecar is the documented upgrade if it ever bites.

The version bump makes an old cache read as empty and be rewritten (the loader already treats a wrong version as empty, `cache.cpp:120-127`), so an upgrade re-analyzes once instead of reading entries that lack the feature.

### D7: The identity feature extractor is a sibling engine with its own interface and factory arm

A `FeatureEngine` with one method (`extract(path) -> eh::Result<std::vector<float>>`) beside `TaggerEngine` (`src/tagger/tagger.h:18`), constructed by a sibling factory function that mirrors `makeTaggerEngine`: `ENCRO_FAKE_TAGGER` provides the fixture vector, Windows builds get the ONNX extractor, and other platforms get an unsupported-platform arm that fails per image the way `UnsupportedPlatformTagger` does (`engine_factory.cpp:100`), preserving today's degradation — no features, so nothing clusters and images fall to `uncategorized/`.

The ONNX extractor reuses the session/provider setup and the GPU-runtime preloading the tagger already performs, binding one input and one output (the tagger binds output 0 only, so the extractor is the simpler case). The fixture format gains an optional vector per image hash so organize tests can drive identity similarity without a model file.

### D8: Preprocessing gains a second filter graph over the shared runner

`runPreprocess` (`preprocess.cpp:29`) keeps the process plumbing, the exact byte-count check and the error reporting; the filter graph becomes a parameter. The tagger keeps its 448×448 letterbox onto a white canvas with `rgb24` at 0-255; the extractor gets 384×384 stretched (no aspect preservation, no overlay) `rgb24`, which the engine then scales by 1/255 and permutes to planar NCHW.

Both graphs are one ffmpeg invocation per image, so an image is decoded twice per analysis pass. Rejected: asking one ffmpeg call for both sizes through a `split` filter graph (one process, but a filter graph far harder to read and to keep in sync with two model contracts), and resizing 448→384 in C++ (a resampler is a dependency the ffmpeg call already is).

## Risks / Trade-offs

- [The published threshold was calibrated on crawled anime art, while the target gallery is AI-generated] → Two galleries already place the published threshold at the best-F1 point of the pair task, and the acceptance task re-measures on the real corpus; the threshold is one constant, so recalibration is a one-line change with evidence.
- [One threshold now serves two different comparisons (image-vs-mean and mean-vs-mean)] → The acceptance task checks the folder-match outcome specifically (how many clusters teaching captures and whether the captures look right). If that measurement demands a second constant, the spec sentence that pins one threshold is what changes, in this change's review, not a silent follow-up.
- [The identity model judges single-character images only] → `mixed/` routing runs before clustering and the pending set is single-subject by construction, which is the same boundary the model is documented for.
- [Dropping idf makes folder names blander on a homogeneous corpus] → Names are the one output the user renames freely (teaching then reuses those names), the identity patterns still exclude scene and expression tags, and frequency ranking reports what the cluster actually shares. A worse name is cosmetic; a worse cluster is not.
- [Analysis now runs two models per image] → Measured while evaluating the models (tagger 26 img/s, extractor 20 img/s on the reference GPU, so roughly 11 img/s combined against 26 today; session measurements, not repo artifacts). The analysis stage already reports rate and ETA, and the work is per image rather than per comparison.
- [An existing model directory makes the next run fail] → The failure names the missing file and offers `--download-models` (the message already lists missing files), and one download run provisions both models. The size the user reads must be updated in the two places that state one — the `--download-models` help text and `README.md:179` — since both say ~400 MB; the download header itself prints no size (`organize_command.cpp:64-68`).
- [Cache entries grow roughly 16×] → Sizes and the upgrade path are recorded in D6; the bounded-batch rewrite design is unchanged, so the cost is I/O per flush, not per image.

## Migration Plan

Users: one `--download-models` run before the next `encro organize`. Existing `organized/` trees stay valid — references are rebuilt from the cached analyses, and the never-rename/never-delete contract is unchanged — but the first run after the upgrade re-analyzes every image (D6) and regroups by the new signal, so `unknown_*` folder names may differ from the previous run's; where a new cluster reaches an existing folder's reference, teaching files it there instead of creating a name.

Rollback: revert the commit. An older build reads the version-2 cache as empty and re-analyzes, so no cache migration is required in either direction.
