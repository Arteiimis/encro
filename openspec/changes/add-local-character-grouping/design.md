# Design: add-local-character-grouping

## Context

encro has no AI or image-analysis capability today. The abandoned branch `add-ai-character-grouping` (never merged) built a cloud-VLM pipeline whose transport (`src/ai`, cpp-httplib), organize scaffolding (scan, SHA-256, cache, execute, report), and their tests are worth porting; its cloud passes (describe/cluster/name/audit via chat completions) are superseded by the local approach decided in exploration. Reusable existing machinery: `media_scanner`, `collision_naming`, the task executor + thread pool, `progress::ProgressContext` (indicators-based bars with ETA), the ffmpeg external-tool orchestration pattern, and the fake-tool e2e test pattern (`fake_media_tool`).

See `proposal.md` for motivation and `specs/image-character-organize/spec.md` for the behavior contract.

## Goals / Non-Goals

**Goals:**

- Every pipeline stage testable offline: no network, no model files in unit/e2e tests.
- Model access isolated behind one seam so the model variant and execution provider never leak into pipeline code.
- Deterministic, resumable runs: content-hash cache, incremental writes, copy-not-move output.
- Zero-trust output naming: auto-generated folder names are always locally sanitized; user renames are never fought.

**Non-Goals:**

- Face/head detection, cropping, per-face multi-character assignment (reserved future tier; see Risks).
- Cloud-assisted anything; rating-based separation; video input; moving/deleting originals.
- GPU providers other than CUDA + CPU (DirectML remains a possible later addition behind the same seam).

## Decisions

### D1: Model and runtime — wd-vit-tagger-v3 ONNX via onnxruntime, CUDA with CPU fallback

`wd-vit-tagger-v3` (SmilingWolf, Hugging Face) as the default model: a Danbooru-trained multi-label classifier (~10k tags: general / character / rating categories) that outputs character tags directly, tolerates sensitive content by construction, and is robust to AI-generated art styles. Runs through `onnxruntime-gpu` 1.22.1 — a local build-repo package (build-repo/packages/o/onnxruntime-gpu) that wraps the official win-x64-gpu release zip WITHOUT the `cuda` xrepo build dependency: the binary only links the import lib, the CUDA/cuDNN DLLs are runtime concerns, and the xrepo `cuda` dep is unsupported under MSYS-shell package envs. The wd-v3 ONNX needs onnxruntime >= 1.17.

Session creation lives in exactly one function with a provider chain: try CUDA EP (`AppendExecutionProvider_CUDA` — the name-based string API does not carry CUDA in ORT 1.19+), then DirectML (compiled into the Windows ORT build, runs on any DX12 GPU), then CPU; the winner is reported on the one provider notice line. Runtime DLL resolution on the target machine (verified during exploration): `scoop install versions/cuda12.9` puts cudart/cublas/cufft on PATH; cuDNN 9 has no scoop/xrepo package, so `--download-models` self-installs it — the pinned cuDNN 9 CUDA-12 archive from NVIDIA's login-free CDN (verified: geo-redirects to `developer.download.nvidia.cn`, ~557 MB) is downloaded, checksum-verified, and its `bin/*.dll` extracted (libzippp, an existing dependency) into `%LOCALAPPDATA%\encro\lib`. encro prepends that lib dir to the process PATH (and calls `AddDllDirectory`) before session creation — PATH prepend is the part plain `LoadLibrary` dependent-DLL resolution actually consults. The cuDNN step is gated on an NVIDIA driver being present (probe `nvcuda.dll`) and, like every download, on `--download-models`.

- *Alternatives*: **DirectML** — no xrepo package and upstream publishes no win-x64 directml zip (verified against the release assets; NuGet-only), so it needs a self-owned package definition; deferred behind the same seam. **CPU-only v1** — rejected by user decision after the throughput comparison. **Other wd-v3 variants** (swinv2/convnext/eva02) — drop-in swappable later via the model dir; vit is the community default.

### D2: Preprocessing entirely in ffmpeg — zero C++ image code

The WD contract is: flatten alpha over white, pad to square, resize to 448×448, feed as NHWC float32 in 0..255 (no normalization), RGB order. The whole transform is one ffmpeg invocation: overlay the decoded image onto a white 448×448 base (correct alpha compositing), scale/pad, emit `rawvideo rgb24` on stdout. C++ receives raw bytes, casts to float, hands to ORT. No stb/libpng/OpenCV dependency; ffmpeg stays the single external tool, discovered via the existing PATH/`--ffmpeg-path` machinery. The exact filter string is pinned by tests (unit via fake ffmpeg fixtures, plus a `[real-ffmpeg]` tagged test).

### D3: Module layout — `src/tagger` (engine seam) vs `src/organize` (pipeline)

- `src/tagger`: `TaggerEngine` interface (image bytes/path in → `TagResult {general, character, rating}` vectors of (tag, confidence) out), `OnnxTagger` (session, preprocessing invocation, tensor build, sigmoid outputs, vocabulary from `selected_tags.csv`), and `FakeTagger` for tests. The pipeline never touches onnxruntime headers.
- `src/organize`: ported scan/SHA-256/cache/execute/report plus new assign (waves), cluster (tag vectors), teach (folder references), naming (sanitizer), and the model store/downloader under `src/tagger` (downloader reuses cpp-httplib, re-added to `xmake.lua` with `ssl=true`).

Dropped from the branch: `src/ai` transport, prompts, cloud describe/cluster/name/audit, thumbnail mosaic machinery, `encro_fake_ai_server`.

### D4: Tag vectors — top-K general tags per image, cosine similarity

The appearance vector is the image's general-category tags at or above the vector evidence floor `kVectorFloor = 0.55` (NOT `--min-confidence`: the 0.35 floor admits the whole ~8k-tag vocabulary, saturating document frequencies and emptying vectors — observed in acceptance; and the 0.5 zero-evidence line still admits noise up to ~0.52), restricted to identity-bearing tags only (hair/eyes/anatomy/signature-accessory patterns minus a scene-word blocklist: `tears`, `cocktail`, `pubic_hair`, expression eyes, ...) and to the trait band (df between `min(corpus/5, max(5, corpus/50))` and 75% of the corpus — collection constants and one-off scene tags are equally useless for telling characters apart), weighted by `confidence x idf`, capped to the top 10. Character and rating tags are excluded from the vector; ratings are cached but per spec never affect layout. Subject-count tags (a fixed design-constant list, e.g. `2girls`, `multiple_boys`) feed the multi-subject routing decision and are excluded from the vector. Both restrictions come from the second acceptance pass on a 1899-image illustration dump: the original unrestricted general-tag vectors collapsed to noise at that scale (pairwise cosine p50 0.08 — scene words dominate, and a `corpus/20` band floor kept only 184 of 1843 tags, all collection-wide), while identity-restricted vectors cluster the same corpus into 70 coherent character groups (same-identity pairs co-cluster 59%); the dumped per-work filename grouping was rejected — the "work ids" in that corpus are daily multi-character posts, so work grouping fights per-character sorting for illustration collections.

### D4a: Character confidence threshold

Acceptance on real inference (task 6.2) showed the character head emits sigmoid(0)≈0.5 for every unused identity, so reusing `--min-confidence` (0.35) for character candidates floods routing with ~2.7k phantom candidates and routes everything to `mixed/`. Character identity therefore uses its own threshold `kCharacterConfidence = 0.60`: AI-generated art sits off the training distribution and depresses character confidence (on the acceptance collection the one real identity fired consistently at 0.6-0.85, never above). Subject-count tags share this threshold — they are subject assertions, not appearance, and their confidences land in the same 0.5-band for off-distribution art. `--min-confidence` keeps governing appearance vectors.

### D5: Clustering — greedy agglomerative assignment to centroids

Deterministic order (content-hash sort): compare each image vector to existing cluster centroids (running mean of L2-normalized vectors); join the best cluster when cosine >= `kClusterTau` (named constant, 0.50 after the second acceptance pass; was 0.82 on the first, smaller corpus — see D4), else open a new cluster. O(n·k) where k is the cluster count — fine at personal-collection scale; no cluster-count input needed.

- *Alternatives*: **DBSCAN/HDBSCAN** — density parameters are harder to explain than a similarity threshold and the library-free implementation is heavier; **k-means** — needs k; **Chinese whispers** — comparable quality, more passes. The greedy centroid walk is the least code with deterministic output.
- *ponytail: order-dependent assignment; a full agglomerative pass is the upgrade path if purity suffers.*

### D5a: Source-work grouping for unclusterable pages

Acceptance on a doujin collection showed comic pages defeat tag-space clustering by design: every page is a different composition, so pairwise vector similarity never reaches the cluster threshold even within one work. Pages whose stem matches a `<work>_<index>` download pattern (reader-app exports) therefore group by their `<work>` prefix after clustering: singleton clusters of the same work land in one `unknown_source_<work>/` folder. This encodes the real prior — one downloaded work shares its character cast — without any hardcoded vocabulary, and turns the user's one-time rename of that folder into teaching material for future runs.

### D6: Teaching — folder references own both a tag vector and a character-tag tally; the cache stores raw analysis, never folder names

At run start, every existing directory under `organized/` becomes a reference computed from the files it contains (files are content-hashed; organized copies hash identically to their originals): (a) a vector = mean of the cached tag vectors of its analyzable members, matched against new cluster centroids (cosine >= `kFolderTau`, 0.65 — identity vectors put same-character folder/cluster pairs at 0.67-0.98 and unrelated pairs at p90 0.32; was 0.80) → adopt the folder's name, highest similarity wins ties); (b) a character-tag tally — a folder claims a tag only when that tag is the sole at-or-above-threshold character candidate for a majority of its analyzable members (members with zero or several candidates contribute nothing, so a renamed `mixed/` folder never claims a tag); ties on the same tag go to the folder with the most tagged members. A claimed tag redirects character-tag assignment to the owning folder's current name: a renamed `hatsune_miku/` keeps receiving Miku images under its new name. Folders with no analyzable members are skipped as references.

The cache stores the raw analysis output per hash — tag/confidence pairs by category (kept at or above the consuming threshold of their category: general >= `kVectorFloor` 0.55, character >= `kWeakConfidence` 0.53, rating >= 0.1), including subject-count tags — and **no folder assignment**. The floors originally planned as a flat 0.1 had to rise: acceptance showed the unused identity head emits ~0.50-0.52 noise for every vocabulary character, so a flat 0.1 floor stored ~2.7k dead pairs per image and dominated the store (~100KB/image) without ever influencing a decision; deriving each floor from the constant that consumes it keeps the cache exactly as large as routing can read. Routing (threshold filtering, multi-subject detection, assignment) is recomputed each run from the cached raw output plus the current folder set, so user renames are authoritative with zero invalidation logic, and every re-run with unchanged options reproduces the original routing deterministically. Lowering a threshold below its category's storage floor requires `--recluster` to rebuild entries stored under the old floors. Persistence is batched — one atomic rewrite per 64 new analyses, flushed at the stage boundary and on the cancel path — because a per-image rewrite made total cache I/O quadratic in collection size (an ~80MB store rewritten per analyzed image collapsed throughput on large collections); a hard kill loses at most the in-flight batch. Duplicate suppression is copy-time skip-existing (same name and content hash). Folder references are recomputed from the on-disk tree every run (the originally planned `folder-vectors.json` fast path was dropped during implementation: rebuild is cheap at personal-collection scale and one less cache-invalidation surface).

### D7: Downloader — manifest with pinned checksums, mirror fallback

A constant manifest maps logical names to {size, sha256, URL templates}: `wd-vit-tagger-v3/model.onnx` and `wd-vit-tagger-v3/selected_tags.csv` from Hugging Face (primary `https://huggingface.co/...`, mirror `https://hf-mirror.com/...`, `HF_ENDPOINT` overrides the primary host), plus the pinned cuDNN 9 CUDA-12 archive from `https://developer.download.nvidia.com/compute/cudnn/redist/...` (checksums from NVIDIA's published redistrib manifest; the CDN's own `.cn` geo-redirect needs no handling). Streaming download to a `.part` file, checksum verify, atomic rename (for cuDNN: verify then extract `bin/*.dll` into the lib dir); on mismatch, bounded re-download. `--download-models` is the only network trigger (spec). The SHA-256 implementation is ported from the branch.

### D8: Progress and reporting — reuse the encode machinery

One `ProgressContext` bar ("Analyzing", completion count postfix with `img/s` rate, ETA via `EtaEstimator`) for the per-image stage; the copy stage reports through the run report, including per-file copy failures so nothing lands nowhere silently. Stage transitions and the final report use the existing `terminal` badge styles. Non-TTY behavior inherits whatever the encode pipeline already does. Ctrl-C stops between images; the cache is flushed on the cancel path, holding everything completed outside the in-flight batch.

### D9: Testing — fake tagger seam, no network, no models

- Unit: `FakeTagger` returns scripted `TagResult`s; assign/cluster/teach/naming/sanitizer are pure functions over them. Downloader tests run against a loopback cpp-httplib fake server (primary-fails→mirror, checksum mismatch, HF_ENDPOINT).
- E2E: `encro_fake_tagger` (env-var-driven fixture mapping content hash → tags, the `fake_media_tool` pattern) so full runs work without models; plus `[real-ffmpeg]` preprocessing tests.
- `[real-model]` tests `SKIP()` when the model dir is absent; the manual validation on a real image set is the acceptance task (cluster purity on the user's collection).

## Risks / Trade-offs

- [Cluster purity on real data unknown] → acceptance task runs the pipeline on the user's collection; `kClusterTau`, top-K, and thresholds are named constants; the Tier-3 upgrade (head detection + crop embedding) is the documented fallback if appearance tags prove too coarse.
- [Lookalike characters merge (same appearance tags)] → inherent ceiling of tag-space clustering; teaching folders cannot split a merged cluster — `--recluster` plus manual splitting of the output folder is the escape hatch.
- [Small/distant subjects produce noisy tags] → such images fall toward `mixed/`/`uncategorized` rather than misfiling.
- [CUDA deployment fragility (cuDNN DLLs, driver)] → CPU fallback guarantees a working run; provider notice names the active provider; scoop/cuDNN drop-dir recipe documented in the README.
- [Model download blocked/slow networks] → mirror fallback + HF_ENDPOINT; checksums prevent silent corruption.
- [ORT/gpu flavor adds build-time cuda deps for all builders] → accepted (personal project, single build serves CPU-only machines).
- [wd-vit contract drift if the model is swapped] → the 448/white-pad/0-255 contract is asserted by a pinned test; a different model variant must satisfy it or the seam needs extending.

## Migration Plan

Purely additive: new subcommand, new modules, new config key, new xmake packages. No existing behavior changes. Rollback is removing the feature; `organized/` trees and caches are user-deletable data. The abandoned branch stays untouched as reference; nothing is migrated from it at runtime.
