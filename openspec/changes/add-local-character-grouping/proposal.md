# Proposal: add-local-character-grouping

## Why

The motivating collection is a folder of AI-generated anime/illustration images that mixes many characters and needs sorting into per-character folders. The explored cloud-VLM approach (branch `add-ai-character-grouping`, never merged) proved unusable: providers reject sensitive images outright, general vision models recognize anime characters poorly, and every image must leave the machine. Character grouping is fundamentally a clustering task that a locally run Danbooru-trained tagger performs without content filtering, with better accuracy on known characters and zero privacy exposure.

## What Changes

- New `encro organize <directory>` subcommand that groups images into per-character folders under `<directory>/organized/` using a fully local, single-pass pipeline: a WD tagger (`wd-vit-tagger-v3`, ONNX) run through onnxruntime.
- Assignment waves:
  1. Images with exactly one character tag at or above `--min-confidence` (default 0.35) file directly into a folder named after the sanitized tag.
  2. Remaining single-subject images cluster by appearance-tag vectors into `unknown_<top-appearance-tags>/` folders.
  3. Multi-subject images — two or more confident character tags, or subject-count tags such as `2girls` — that do not have exactly one confident character tag land in `mixed/`.
- Teaching by rename: user-renamed output folders (any charset, including CJK) become naming references that later runs match new clusters against; encro never renames or deletes output folders.
- Rating tags are computed and cached but never affect the folder layout.
- Progress bar with image rate and ETA during analysis (reusing the encode progress machinery); end-of-run report listing per-folder counts and each folder's assignment source.
- GPU acceleration: the tagger session runs on the CUDA execution provider when the CUDA runtime is available (RTX-class target machine verified), falling back to the CPU provider with a one-line notice otherwise; session creation is a single seam so other providers can attach later. The missing cuDNN runtime is self-installed: `--download-models` fetches the pinned cuDNN 9 archive from NVIDIA's login-free CDN (geo-redirecting to the China CDN) into the encro lib dir when an NVIDIA driver is present; the CUDA toolkit DLLs (cudart/cublas/cufft) come from a one-time `scoop install versions/cuda12.9`.
- Model management: models resolve from `--model-dir` (default `~/.encro/models`); `--download-models` fetches missing model files from Hugging Face with an hf-mirror.com fallback and checksum verification; nothing is ever downloaded without the flag.
- Resume support: SHA-256 content-hash cache under `<directory>/organized/.cache/`; `--dry-run` prints the plan without copying; `--recluster` discards cached analysis.
- Ported from the abandoned branch: scan, SHA-256, cache, execute, report, and folder-name sanitization, with their tests. Discarded: cloud transport (`src/ai`), the cloud describe/cluster/name/audit passes, and the fake AI server.
- Non-goals: cloud-assisted naming; face/head detection and per-face multi-character assignment (reserved as a future tier); sensitive-content separation; video input; moving or deleting originals; zip output of grouped folders.

## Capabilities

### New Capabilities

- `image-character-organize`: the `encro organize` capability — local-only analysis, character-tag assignment, appearance-tag clustering, rename-teaching, the `mixed/` bucket, cache/resume, model download, progress display, and the run report.

### Modified Capabilities

- `user-config`: the configurable key set grows by `model-dir` (directory holding local model files), subject to the same set/get/unset/reject rules as existing keys.

## Impact

- **New code**: `src/tagger` (ONNX session wrapper, tag vocabulary loading, preprocessing contract) and `src/organize` (scan, assign, cluster, teach, execute, report, cache) — partially ported from the abandoned branch.
- **Modified code**: `src/cmd` (subcommand and option registration, config key), `xmake.lua` (onnxruntime package added).
- **Dependencies**: onnxruntime (xrepo prebuilt, `gpu=true` CUDA flavor with CPU-provider fallback), libzippp (existing dependency, used to extract the cuDNN archive), ffmpeg (image decoding via the existing external-tool pattern, now also feeding the tagger).
- **Runtime artifacts**: model files (~350 MB) under the model dir; per-run cache under `<directory>/organized/.cache/`.
- **Tests**: ported unit tests for the salvaged stages; new unit tests behind a fake-tagger seam; e2e tests driven by a fixture-based fake tagger (no network, no model files); manual validation on a real image set as the acceptance step.
- **Supersedes**: branch `add-ai-character-grouping` (not merged; nothing to migrate).
