## MODIFIED Requirements

### Requirement: Appearance clustering for unassigned images

Routing follows a fixed order: assignment by exactly one at-or-above-threshold character tag first, then multi-subject routing to `mixed/`, then clustering of the single-subject remainder. Single-subject images with no character tag at or above the threshold SHALL be clustered by their identity feature: the fixed-width numeric embedding the identity model produces for the image, normalized to unit length before use, so that similarity is a pure cosine and an image's stored magnitude never influences a cluster's shape.

An image joins the cluster whose centroid (the mean of its members' normalized features) is most similar, provided that similarity reaches the identity similarity threshold (a design constant, default 0.643 — the cosine equivalent of the identity model's published metric threshold, whose shipped score is a fixed decreasing function of the cosine, `0.5 × (1 − cosine)`, identical across the model family and applied in-process rather than by a second model file). Otherwise the image opens a new cluster. Clustering SHALL NOT require the number of clusters to be specified in advance, and SHALL NOT depend on any corpus-wide statistic fitted at run time.

Clusters SHALL be named `unknown_<top-appearance-tags>`, sanitized and length-capped, with deterministic collision suffixes; the tags are read from the cluster members' own general tags, restricted to identity-bearing tags (hair, eyes, anatomy, and signature-accessory patterns; scene and action words describe the picture, not the person, and shall not name a cluster), ranked by how many members carry them. The embedding itself carries no labels, so naming stays tag-derived. A singleton cluster whose file stem matches a `<work>_<index>` download pattern SHALL be grouped with the other singleton clusters of the same `<work>` into one `unknown_source_<work>/` folder when at least two such pages exist (pages of one work share its character cast); pages that match no group and no cluster fall to `uncategorized/`.

#### Scenario: Same original character across styles clusters together

- **WHEN** images of the same original character in differing art styles produce identity features whose cosine similarity to the same cluster centroid reaches the identity similarity threshold, and no character tag reaches the threshold
- **THEN** they land in one cluster and one folder

#### Scenario: Similarity below the threshold opens a new cluster

- **WHEN** an image's identity feature is similar to every existing cluster centroid, but below the identity similarity threshold
- **THEN** the image opens a new cluster instead of joining the most similar one

#### Scenario: Cluster names describe appearance

- **WHEN** a cluster's members most frequently carry the identity tags `pink_hair` and `blue_eyes`
- **THEN** its folder name contains those tags in the `unknown_` prefix form

#### Scenario: Distinct clusters with identical tag descriptions do not collide

- **WHEN** two distinct clusters reduce to the same descriptive name
- **THEN** deterministic numeric suffixes keep the folders distinct

### Requirement: Teaching by renamed folders

At the start of every run, existing folders under the output root (including folders the user renamed, in any character set) SHALL be consulted as naming references: a new cluster whose identity-feature centroid matches a reference folder's mean identity feature at or above the identity similarity threshold SHALL be filed into that folder's name instead of receiving an `unknown_` name. A reference folder's mean feature is the mean of its analyzable members' normalized features, read from cached analysis. Additionally, character-tag assignment SHALL respect folder ownership: when a reference folder's contents identify with a character tag (that tag being carried as the sole at-or-above-threshold character tag by a majority of the folder's analyzable members), images assigned that character tag SHALL be filed into the owning folder's current name instead of the sanitized tag name; when several folders claim the same tag, the folder with the most tagged members wins. Folders containing no analyzable member files, or no member with a cached identity feature, SHALL be skipped as references. encro SHALL never rename, delete, or reorganize existing output folders. User-renamed folders SHALL take precedence over both `unknown_` naming and raw tag-derived names.

#### Scenario: Renamed cluster folder teaches the next run

- **WHEN** a previous run produced `unknown_pink_hair_blue_eyes/`, the user renamed it to `我的角色`, and a later run finds a matching cluster
- **THEN** the new cluster's images are copied into `我的角色`

#### Scenario: A dissimilar folder does not capture a cluster

- **WHEN** a run finds clusters and existing folders, but no reference folder's mean feature reaches the identity similarity threshold against a cluster's centroid
- **THEN** that cluster keeps an `unknown_` name instead of being filed into a folder

#### Scenario: Renamed character folder teaches the next run

- **WHEN** a previous run produced `hatsune_miku/` from character tags, the user renamed it to `初音ミク`, and a later run analyzes images tagged `hatsune_miku`
- **THEN** those images are copied into `初音ミク` and no `hatsune_miku` folder is recreated

#### Scenario: Existing folders are never modified

- **WHEN** a run completes with reference folders present
- **THEN** every pre-existing folder still exists under its original name and its previous contents are unchanged

### Requirement: Cache and resume

Analysis results SHALL be cached keyed by SHA-256 of file content under `<directory>/organized/.cache/`, and SHALL hold everything a later run needs to skip re-analysis: the identity feature and the tag pairs. Re-runs SHALL skip analysis for unchanged images (renames and moves still hit the cache). The cache SHALL be persisted in bounded batches (not one rewrite per image) and flushed at the analysis stage boundary and on interruption, so an interrupted run (including Ctrl-C) resumes without redoing completed analysis beyond the in-flight batch. Stored tag pairs SHALL be limited to each category's consuming threshold (general at or above the naming floor, character at or above the weakest routing threshold) so identity noise cannot dominate the store. A cache written by a different cache format version SHALL be treated as empty and rewritten, so an upgrade re-analyzes once instead of reading entries that lack the identity feature. `--recluster` SHALL discard cached analysis before running. Cached rating tags SHALL never influence folder assignment.

#### Scenario: Re-run does not re-analyze

- **WHEN** a run completes and the same directory is scanned again with no changes
- **THEN** no model inference runs for the already-cached images

#### Scenario: Interrupted run resumes

- **WHEN** a run is interrupted mid-analysis and re-run
- **THEN** analysis continues from the cache and previously analyzed images are not re-processed

#### Scenario: Cache from an earlier format is discarded

- **WHEN** a cache written by an earlier cache format version is present at the start of a run
- **THEN** the run analyzes every image again and rewrites the cache in the current format

### Requirement: Model and runtime file management

Model files SHALL resolve from `--model-dir` (default `~/.encro/models`; persistable via config). The command SHALL require two models — the tagger that produces the tags and the identity model that produces the identity feature — and every model file SHALL be pinned by size and checksum in the one file list the download and presence checks share, so a new model cannot be added to one path only. When required model files are missing, the run SHALL fail fast with a message that names every missing file and offers `--download-models` and the manual-download alternative. `--download-models` SHALL download missing model files from the primary Hugging Face URL, falling back to the `hf-mirror.com` mirror per-file on failure, honoring an `HF_ENDPOINT` override, and SHALL verify each downloaded file against a pinned checksum before accepting it. When an NVIDIA driver is present and the cuDNN runtime DLLs are missing, `--download-models` SHALL also install the pinned cuDNN archive (downloaded from NVIDIA's public CDN, checksum-verified) by extracting its DLLs into encro's lib directory; on machines without an NVIDIA driver it SHALL skip that download. No download SHALL ever happen without `--download-models`.

#### Scenario: Missing models fail fast with guidance

- **WHEN** `encro organize <dir>` runs with no model files in the model dir
- **THEN** it exits non-zero before scanning, listing every missing file and both remedies

#### Scenario: A missing identity model fails like a missing tagger

- **WHEN** the tagger files are present but the identity model file is missing
- **THEN** the run exits non-zero before scanning, names the missing identity file, and offers `--download-models`

#### Scenario: One download run provisions both models

- **WHEN** `--download-models` runs against a model directory that lacks both models
- **THEN** every missing model file downloads and verifies, and a following `organize` run needs no download

#### Scenario: Mirror fallback completes the download

- **WHEN** `--download-models` runs and the primary URL fails but the mirror serves the file
- **THEN** the file downloads from the mirror and passes checksum verification

#### Scenario: Checksum mismatch is rejected

- **WHEN** a downloaded file's digest does not match the pinned checksum
- **THEN** the file is rejected, the failure is reported, and the run does not proceed with it

#### Scenario: cuDNN self-installed on an NVIDIA machine

- **WHEN** `--download-models` runs on a machine with an NVIDIA driver but without cuDNN DLLs in encro's lib directory
- **THEN** the pinned cuDNN archive downloads, its DLLs are extracted into the lib directory, and the CUDA provider initializes on the retry

#### Scenario: No cuDNN download without an NVIDIA driver

- **WHEN** `--download-models` runs on a machine without an NVIDIA driver
- **THEN** no cuDNN archive is downloaded and the run proceeds on the CPU provider
