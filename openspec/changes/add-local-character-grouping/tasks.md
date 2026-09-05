# Tasks: add-local-character-grouping

## 1. Setup and porting

- [x] 1.1 Add `onnxruntime` (pinned 1.22.x, `gpu=true`) and `cpp-httplib` (`ssl=true`) to `xmake.lua`; verify `xmake build encro` links an Ort session-creation snippet on clang-cl
- [x] 1.2 Port `scan` and `sha256` with their tests from branch `add-ai-character-grouping`, adding the `organized/`-tree exclusion with a test (spec: scan excludes the output tree); verify the ported `[organize]` tests pass
- [x] 1.3 Port the content-hash cache, adapted to store the raw analysis output per hash (tag/confidence pairs by category truncated at a 0.1 confidence floor, including subject-count tags and character-tag candidates — thresholds are applied at routing time, not baked into the cache) and no folder assignment; unit-test keying by SHA-256 (rename/move still hits), incremental write, resume-from-partial, and the truncation floor
- [x] 1.4 Port `execute` (add skip-existing when target name and content hash match) and the `report` skeleton; unit-test untouched originals, exactly-one-folder copies, and re-run-after-rename creating no duplicate folders

## 2. Tagger engine (`src/tagger`) — test first

- [x] 2.1 Define the `TaggerEngine` seam and `TagResult` types; unit-test the tag-vocabulary loader against a `selected_tags.csv` fixture (general/character/rating category split, id mapping)
- [x] 2.2 Implement the preprocessing contract as one ffmpeg invocation (white-base overlay for alpha, 448x448 pad, `rawvideo rgb24` stdout); unit-test the argument building against a fake-tool fixture and add a `[real-ffmpeg]` test asserting 448*448*3 bytes of decodable output
- [x] 2.3 Implement `OnnxTagger`: single-seam session creation (CUDA EP append, CPU fallback, one notice line, lib-dir PATH prepend + `AddDllDirectory`), NHWC float32 0..255 tensor build, output-to-tags mapping; unit-test tensor build and mapping as pure functions, plus a `[real-model]` smoke test that SKIPs when the model dir is absent
- [x] 2.4 Implement the model store and downloader: manifest with pinned sha256/size, `.part` streaming, atomic rename, bounded retry on checksum mismatch, Hugging Face -> hf-mirror fallback with `HF_ENDPOINT` override, cuDNN self-install gated on `nvcuda.dll` presence (download, verify, extract `bin/*.dll` via libzippp into the encro lib dir); unit-test all paths against a loopback fake server including mirror fallback, checksum rejection, HF_ENDPOINT, and cuDNN skip without an NVIDIA driver

## 3. Assignment and clustering (pure functions) — test first

- [ ] 3.1 Implement character-tag assignment: exactly one tag >= `--min-confidence` -> sanitized `[a-z0-9_]` folder name with collision suffixes; two or more -> multi-subject marker; unit-test threshold edges and sanitization
- [ ] 3.2 Implement appearance tag-vector building (general tags >= threshold, top-K by confidence, sparse multi-hot) and cosine similarity; unit-test top-K capping and background-tag exclusion
- [ ] 3.3 Implement greedy agglomerative clustering (hash-sorted order, centroid join at `kClusterTau`, else new cluster) and `unknown_<top-tags>` naming with deterministic collision suffixes; unit-test determinism, same-character-across-styles grouping, and distinct-cluster name collisions
- [ ] 3.4 Implement teaching: folder references computed from folder contents (content-hash lookup) carrying both a mean tag vector and a character-tag tally; cluster-to-folder matching at `kFolderTau`; character-tag assignment targeting the owning folder's current name; user renames authoritative (no folder names in cache); `folder-vectors.json` fast path rebuildable from contents; unit-test rename-teaching for both cluster folders (including a CJK folder name) and character folders (renamed `hatsune_miku` keeps receiving its images, no old-name folder recreated), never-modify-existing-folders, and vector-rebuild after cache wipe

## 4. Pipeline wiring

- [ ] 4.1 Orchestrate scan -> analyze -> assign/cluster/teach -> execute -> report through the existing task executor; wire the analyze-stage `ProgressContext` bar (count, img/s, ETA via `EtaEstimator`) and provider notice; unit-test staging with a fake tagger
- [ ] 4.2 Implement `mixed/` and `uncategorized/` semantics; unit-test multi-subject routing (two-or-more confident character tags, or subject-count tags such as `2girls` without exactly one confident character tag -> `mixed/`), analysis failure -> `uncategorized/`, and every image landing in exactly one folder
- [ ] 4.3 Implement `--dry-run` (full analysis, report, zero copies), `--recluster` (discard cached analysis), and Ctrl-C-safe incremental cache writes with resume issuing no re-analysis; unit-test each
- [ ] 4.4 Extend the report to list per-folder counts and assignment sources (character tag / folder match / new cluster / mixed / uncategorized) plus run totals; unit-test the rendered report

## 5. CLI and e2e

- [ ] 5.1 Register `encro organize` with `-r/--recursive`, `--min-confidence`, `--model-dir`, `--download-models`, `--dry-run`, `--recluster`; add the `model-dir` config key with validation and precedence (CLI > config > default); verify help output shows the privacy line and options, config set/get accept/reject, and completion registry entries update
- [ ] 5.2 Add the `encro_fake_tagger` fixture tool (env-var-driven content-hash -> tags mapping, the `fake_media_tool` pattern) and e2e tests: happy path grouping, resume without duplicate analysis, rename-teaching across runs, dry-run copies nothing, missing models fail fast with guidance; assert classification exercises no network path (the downloader runs only behind `--download-models`)

## 6. Final verification

- [ ] 6.1 `xmake test-parallel` green; new tests tagged (`[organize]`, `[tagger]`, `[e2e]`); `xmake fmt -k` clean; `xmake tidy` no new findings in `src/tagger` / `src/organize`
- [ ] 6.2 Real-machine acceptance on the RTX 3070 laptop: `--download-models` installs model + cuDNN (CUDA provider line confirmed), then a run over a real collection sample (20-30+ images) — record cluster purity, unknown-folder quality, and throughput; tune `kClusterTau`/top-K constants from the observations
- [ ] 6.3 Document the GPU setup story (one-time `scoop install versions/cuda12.9`, self-installed cuDNN, CPU fallback) and `encro organize` usage in the README
