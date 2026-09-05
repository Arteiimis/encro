# Delta Spec: image-character-organize

## Purpose

Groups the images of a directory into per-character folders under `<directory>/organized/` so that a mixed collection (for example AI-generated anime illustrations) can be browsed per character. All analysis runs locally; image content never leaves the machine.

## ADDED Requirements

### Requirement: Command surface and scanning

`encro organize <directory>` SHALL scan the directory for still-image files (`.jpg`, `.jpeg`, `.png`, `.webp`), skipping the `organized/` output tree itself. Scanning SHALL be non-recursive by default and recursive with `-r/--recursive`; the `recursive` config key applies with the standard CLI > config > default precedence, and the built-in default is non-recursive. An empty scan result SHALL exit 0 with a message stating that no images were found. A nonexistent or unreadable target directory SHALL exit non-zero with an error naming the path. The subcommand help SHALL state that analysis is local and that images are not uploaded.

#### Scenario: Default scan excludes subdirectories and output tree

- **WHEN** the target directory contains images directly, images in a subdirectory, and an `organized/` tree from a previous run
- **THEN** only the images directly in the target directory are processed

#### Scenario: Recursive flag includes subdirectories

- **WHEN** `encro organize <dir> -r` runs over a tree that also contains an `organized/` output tree
- **THEN** images under subdirectories are processed and the `organized/` tree is still excluded

#### Scenario: No images found

- **WHEN** the target directory contains no supported image files
- **THEN** the command exits 0 and prints a message that no images were found

#### Scenario: Nonexistent directory fails

- **WHEN** `encro organize` runs against a directory that does not exist or cannot be read
- **THEN** the command exits non-zero with an error naming the directory

### Requirement: Local-only analysis

All classification decisions SHALL be computed on the local machine by model files resolved from the model directory. The pipeline SHALL make no network requests while classifying, except the explicit model download triggered by `--download-models`.

#### Scenario: Classification performs no network access

- **WHEN** `encro organize <dir>` runs with models already present and machine network access is blocked
- **THEN** the run completes successfully with identical results to an unblocked run

### Requirement: Known-character assignment

For every image, the analyzer SHALL produce character-tag candidates with confidence values. Character confidence is tiered: a candidate is confident at or above the character confidence threshold (a design constant, default 0.60 — character heads emit ~0.5 confidence for every unused identity so the threshold must sit above `--min-confidence`, and AI-generated art sits off the training distribution which further depresses confidence), and weak-but-identity-bearing above 0.5 (the zero-evidence floor: a zero logit). An image with exactly one confident candidate SHALL be assigned to a folder named after that tag; when no confident candidate exists but exactly one weak candidate does, the image SHALL be assigned to that tag's folder as well (a second, uncharacterized subject does not make the image ownerless). Folder names are sanitized to `[a-z0-9_]` with deterministic collision suffixes. An image with two or more confident character tags SHALL be treated as multi-subject.

#### Scenario: Single confident character tag names the folder

- **WHEN** an image's highest character tag `hatsune_miku` scores 0.9 and no other character tag scores at or above the character confidence threshold
- **THEN** the image is assigned to the folder `hatsune_miku`

#### Scenario: Tag below threshold is ignored

- **WHEN** a single-subject image's best character tag scores 0.2 against a 0.35 threshold
- **THEN** the image is not assigned by character tag and proceeds to clustering

#### Scenario: Two confident character tags are multi-subject

- **WHEN** an image has two character tags scoring at or above the threshold
- **THEN** the image is treated as multi-subject for assignment purposes

### Requirement: Appearance clustering for unassigned images

Routing follows a fixed order: assignment by exactly one at-or-above-threshold character tag first, then multi-subject routing to `mixed/`, then clustering of the single-subject remainder. Single-subject images with no character tag at or above the threshold SHALL be clustered by their appearance-tag vectors. A vector contains general tags whose confidence reaches the vector evidence floor (a design constant above the 0.5 zero-evidence band — thresholds below it let the full multi-thousand-tag vocabulary through and destroy discrimination), restricted to the trait band: tags whose corpus document frequency is at least a scaled minimum and at most 75% of the corpus (collection-constant tags describe the collection, one-off tags describe the scene; neither separates characters), weighted by confidence times inverse document frequency and capped to the top K. Clusters SHALL be named `unknown_<top-appearance-tags>`, sanitized and length-capped, with deterministic collision suffixes. Clustering SHALL NOT require the number of clusters to be specified in advance. A singleton cluster whose file stem matches a `<work>_<index>` download pattern SHALL be grouped with the other singleton clusters of the same `<work>` into one `unknown_source_<work>/` folder when at least two such pages exist (pages of one work share its character cast); pages that match no group and no cluster fall to `uncategorized/`.

#### Scenario: Same original character across styles clusters together

- **WHEN** images of the same original character in differing art styles share most high-confidence appearance tags and no character tag reaches the threshold
- **THEN** they land in one cluster and one folder

#### Scenario: Cluster names describe appearance

- **WHEN** a cluster's most frequent appearance tags are `pink_hair` and `blue_eyes`
- **THEN** its folder name contains those tags in the `unknown_` prefix form

#### Scenario: Distinct clusters with identical tag descriptions do not collide

- **WHEN** two distinct clusters reduce to the same descriptive name
- **THEN** deterministic numeric suffixes keep the folders distinct

### Requirement: Multi-subject fallback

An image is multi-subject when it has two or more confident character tags, two or more weak competing character candidates with no confident candidate, or a subject-count tag (a fixed vocabulary of general tags such as `2girls`) asserted at or above the strong count threshold with no character candidate above the zero-evidence floor. A multi-subject image with exactly one confident character tag SHALL be assigned to that character's folder; every other multi-subject image SHALL be copied into `mixed/`.

#### Scenario: Two confident character tags go to mixed

- **WHEN** an image has two character tags at or above the character confidence threshold
- **THEN** the image is copied into `mixed/`

#### Scenario: Subject-count tags without a character tag go to mixed

- **WHEN** an image carries a multi-subject count tag (for example `2girls`) at or above the character confidence threshold and no confident character tag
- **THEN** the image is copied into `mixed/`

#### Scenario: One known character among several subjects files by that character

- **WHEN** a multi-subject image has exactly one confident character tag
- **THEN** the image is assigned to that character's folder

### Requirement: Teaching by renamed folders

At the start of every run, existing folders under the output root (including folders the user renamed, in any character set) SHALL be consulted as naming references: a new cluster whose appearance-tag vector matches a reference folder's vector within a similarity threshold SHALL be filed into that folder's name instead of receiving an `unknown_` name. Additionally, character-tag assignment SHALL respect folder ownership: when a reference folder's contents identify with a character tag (that tag being carried as the sole at-or-above-threshold character tag by a majority of the folder's analyzable members), images assigned that character tag SHALL be filed into the owning folder's current name instead of the sanitized tag name; when several folders claim the same tag, the folder with the most tagged members wins. Folders containing no analyzable member files SHALL be skipped as references. encro SHALL never rename, delete, or reorganize existing output folders. User-renamed folders SHALL take precedence over both `unknown_` naming and raw tag-derived names.

#### Scenario: Renamed cluster folder teaches the next run

- **WHEN** a previous run produced `unknown_pink_hair_blue_eyes/`, the user renamed it to `我的角色`, and a later run finds a matching cluster
- **THEN** the new cluster's images are copied into `我的角色`

#### Scenario: Renamed character folder teaches the next run

- **WHEN** a previous run produced `hatsune_miku/` from character tags, the user renamed it to `初音ミク`, and a later run analyzes images tagged `hatsune_miku`
- **THEN** those images are copied into `初音ミク` and no `hatsune_miku` folder is recreated

#### Scenario: Existing folders are never modified

- **WHEN** a run completes with reference folders present
- **THEN** every pre-existing folder still exists under its original name and its previous contents are unchanged

### Requirement: Output semantics

The run SHALL copy (never move) each scanned image into exactly one folder under `<directory>/organized/`; originals SHALL be untouched. A copy target that already exists with identical content (matching content hash) SHALL be skipped silently. Name collisions with different content SHALL receive deterministic numeric suffixes. An image whose analysis fails SHALL be copied into `uncategorized/` so every scanned image lands in exactly one folder.

#### Scenario: Originals untouched

- **WHEN** a run copies images into `organized/`
- **THEN** every original file remains at its original path with unmodified content

#### Scenario: Re-run after a rename neither duplicates nor recreates

- **WHEN** a run is repeated over the same directory after the user renamed an output folder (cluster or character folder)
- **THEN** images are assigned into the renamed folder, files already present there with identical content are skipped, and no folder under the pre-rename name is created

#### Scenario: Analysis failure degrades to uncategorized

- **WHEN** analysis of an image fails (for example an undecodable file)
- **THEN** the run continues and that image is copied into `uncategorized/`

### Requirement: Cache and resume

Analysis results SHALL be cached keyed by SHA-256 of file content under `<directory>/organized/.cache/`. Re-runs SHALL skip analysis for unchanged images (renames and moves still hit the cache). The cache SHALL be persisted in bounded batches (not one rewrite per image) and flushed at the analysis stage boundary and on interruption, so an interrupted run (including Ctrl-C) resumes without redoing completed analysis beyond the in-flight batch. Stored pairs SHALL be limited to each category's consuming threshold (general at or above the vector floor, character at or above the weakest routing threshold) so identity noise cannot dominate the store. `--recluster` SHALL discard cached analysis before running. Cached rating tags SHALL never influence folder assignment.

#### Scenario: Re-run does not re-analyze

- **WHEN** a run completes and the same directory is scanned again with no changes
- **THEN** no model inference runs for the already-cached images

#### Scenario: Interrupted run resumes

- **WHEN** a run is interrupted mid-analysis and re-run
- **THEN** analysis continues from the cache and previously analyzed images are not re-processed

### Requirement: Model and runtime file management

Model files SHALL resolve from `--model-dir` (default `~/.encro/models`; persistable via config). When required model files are missing, the run SHALL fail fast with a message that offers `--download-models` and the manual-download alternative. `--download-models` SHALL download missing model files from the primary Hugging Face URL, falling back to the `hf-mirror.com` mirror per-file on failure, honoring an `HF_ENDPOINT` override, and SHALL verify each downloaded file against a pinned checksum before accepting it. When an NVIDIA driver is present and the cuDNN runtime DLLs are missing, `--download-models` SHALL also install the pinned cuDNN archive (downloaded from NVIDIA's public CDN, checksum-verified) by extracting its DLLs into encro's lib directory; on machines without an NVIDIA driver it SHALL skip that download. No download SHALL ever happen without `--download-models`.

#### Scenario: Missing models fail fast with guidance

- **WHEN** `encro organize <dir>` runs with no model files in the model dir
- **THEN** it exits non-zero before scanning, listing the missing files and both remedies

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

### Requirement: Execution provider selection

The analyzer SHALL attempt the CUDA execution provider first and fall back to the CPU provider when CUDA initialization fails, printing exactly one notice line naming the active provider. Provider selection SHALL never fail a run by itself.

#### Scenario: CUDA unavailable falls back with a notice

- **WHEN** the CUDA runtime DLLs are absent or initialization fails
- **THEN** the run proceeds on the CPU provider and prints one notice naming the CPU provider

#### Scenario: Active provider is reported

- **WHEN** a run starts
- **THEN** exactly one line names the execution provider in use

### Requirement: Progress and report

During analysis the command SHALL show a progress bar with completion count, image rate, and ETA on a TTY; no progress bar SHALL be rendered when stdout is not a terminal. After execution the command SHALL print a report: per-folder counts, each folder's assignment source (character tag, folder match, new cluster, `mixed/`, `uncategorized/`), and run totals.

#### Scenario: Analysis shows progress with rate and ETA

- **WHEN** analysis runs on a TTY with more than a few images
- **THEN** a progress bar shows images completed, images per second, and an ETA

#### Scenario: Report lists folders with sources

- **WHEN** a run completes with folders from character tags, folder matches, and new clusters
- **THEN** the report lists each folder with its image count and assignment source

### Requirement: Dry run

`--dry-run` SHALL run the full analysis and print the assignment plan (the report) without copying anything and without creating output folders beyond the cache.

#### Scenario: Dry run copies nothing

- **WHEN** `encro organize <dir> --dry-run` runs
- **THEN** the report is printed, no `organized/<name>/` folders are created, and no images are copied
