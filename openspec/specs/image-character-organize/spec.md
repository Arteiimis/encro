# image-character-organize Specification

## Purpose

Groups the images of a directory into per-character folders under `<directory>/organized/` so that a mixed collection (for example AI-generated anime illustrations) can be browsed per character. All analysis runs locally; image content never leaves the machine.

## Requirements

### Requirement: Command surface and scanning

`encro organize <directory>` SHALL scan the directory for still-image files (`.jpg`, `.jpeg`, `.png`, `.webp`), skipping the `organized/` output tree itself. The set is matched case-insensitively (`media-scan`), so uppercase and mixed-case spellings of these extensions are scanned too. Scanning SHALL be non-recursive by default and recursive with `-r/--recursive`. In an incremental run (non-recursive, first-level subfolders besides the output tree exist — the incremental requirement defines the dispositions) the scan input SHALL instead be the root's loose images plus the images of the input-disposition folders collected recursively; reference and ignored folders SHALL never contribute scan input — a reference folder's members are read only by the sampling the incremental requirement defines, which profiles rather than processes them. In a recursive run, every first-level folder not named by `--ignore-folder` holds the input disposition, so the whole tree is scanned as it was before. The `recursive` config key applies with the standard CLI > config > default precedence, and the built-in default is non-recursive. An empty scan result SHALL exit 0 with a message stating that no images were found. A nonexistent or unreadable target directory SHALL exit non-zero with an error naming the path. The subcommand help SHALL state that analysis is local and that images are not uploaded.

`--ingest <name>` (repeatable) SHALL set a first-level folder's disposition to input, and `--ignore-folder <name>` (repeatable) SHALL set it to ignored. Both names SHALL be validated before any scan: a name that is not a first-level directory of the target besides the output tree SHALL be rejected as an argument error naming the flag and the name, and the same name given to both flags SHALL be rejected as an argument error.

The directory SHALL be optional when `--download-models` is given: in that form the command provisions the model files, reports the model directory it provisioned, and exits 0 without scanning and without copying anything, because fetching the models needs no images. With a directory, `--download-models` keeps fetching what is missing before the run, and the model-presence check applies to both forms. Without a directory and without `--download-models`, the command SHALL exit non-zero with the argument error naming the directory and the help hint. The subcommand help SHALL show the directory as optional in its usage and SHALL state, for `--download-models`, the approximate download size and that the models are fetched and the run stops there when no directory is given.

`--identity-tau <value>` SHALL set the identity similarity threshold that decides whether an image or a cluster belongs with a cluster or an existing folder, replacing the built-in calibrated default; the `identity-tau` config key applies with the standard CLI > config > default precedence. The value SHALL be validated to the open interval (0, 1] and an out-of-range value SHALL be rejected as an argument error before any scan. The subcommand help SHALL name the option, state its built-in default and state that a higher value keeps look-alike characters apart while a lower one keeps a character's varied artwork together.

#### Scenario: Default scan excludes subdirectories and output tree

- **WHEN** a non-recursive run's target directory contains images directly, images in a first-level subdirectory that is not on the miscellaneous-name list, and an `organized/` tree from a previous run
- **THEN** only the images directly in the target directory are input; the subdirectory is a reference — up to the sample-size constant of its images may be analyzed to profile it — and its images are never routed, copied, or modified

#### Scenario: Uppercase image extensions are scanned

- **WHEN** the target directory contains an image named `PHOTO.JPG`
- **THEN** the image is scanned and processed like its lowercase spelling

#### Scenario: Recursive flag includes subdirectories

- **WHEN** `encro organize <dir> -r` runs over a tree that also contains an `organized/` output tree
- **THEN** images under subdirectories are processed and the `organized/` tree is still excluded

#### Scenario: Ingest folders contribute their images recursively

- **WHEN** `encro organize <dir> --ingest mix` runs non-recursively and `mix/` contains images directly and in a nested subfolder
- **THEN** both sets of images are scanned as input and the `mix/` folder itself is not a reference

#### Scenario: An ingest name that is not a first-level folder is an argument error

- **WHEN** `encro organize <dir> --ingest nonexistent` runs
- **THEN** the command fails with an argument error naming `--ingest` and `nonexistent` before any scan

#### Scenario: The same folder in both flags is an argument error

- **WHEN** `encro organize <dir> --ingest mix --ignore-folder mix` runs
- **THEN** the command fails with an argument error naming the folder before any scan

#### Scenario: No images found

- **WHEN** the target directory contains no supported image files
- **THEN** the command exits 0 and prints a message that no images were found

#### Scenario: Nonexistent directory fails

- **WHEN** `encro organize` runs against a directory that does not exist or cannot be read
- **THEN** the command exits non-zero with an error naming the directory

#### Scenario: Download-only run needs no directory

- **WHEN** `encro organize --download-models` runs without a directory
- **THEN** the missing model files are fetched, the model directory is named, no directory is scanned, nothing is copied, and the command exits 0

#### Scenario: A missing directory without the download flag is an argument error

- **WHEN** `encro organize` runs with no directory and without `--download-models`
- **THEN** the command exits non-zero with the argument error naming `dir` and the help hint, before any model check or scan

#### Scenario: Help shows the conditional directory

- **WHEN** `encro organize -h` runs
- **THEN** the usage line shows the directory as optional and the `--download-models` help states that the models are fetched and the run stops there when no directory is given

#### Scenario: A tuned identity threshold replaces the default

- **WHEN** `encro organize <dir> --identity-tau 0.72` runs, or the `identity-tau` config key is set and no flag is given
- **THEN** clustering and folder matching decide at 0.72 instead of the built-in default, and the help for the subcommand names the option and states its default

#### Scenario: An out-of-range identity threshold is an argument error

- **WHEN** `encro organize <dir> --identity-tau 1.5` runs
- **THEN** the command fails with an argument error before scanning any image

### Requirement: Incremental organize over an existing folder structure

When a non-recursive run's target directory contains first-level subfolders besides the output tree (`organized/`), the run SHALL operate as an incremental organize: every first-level folder SHALL receive exactly one disposition — reference, input, or ignored — and the run SHALL file the remainder (the root's loose images and the input folders' images, collected recursively) into skeleton folders under the output tree that mirror reference folders by their exact names. The disposition of a first-level folder SHALL be decided in this order: a folder named by `--ingest` is input, a folder named by `--ignore-folder` is ignored, a folder whose trimmed name matches the built-in miscellaneous-name list is input, and every other folder is a reference. Dot-prefixed first-level folders SHALL be ignored unless `--ingest` names them. The miscellaneous-name list SHALL be a fixed design constant of common miscellaneous folder names in English, Simplified Chinese, Traditional Chinese and Japanese, matched against the whole trimmed folder name exactly — ASCII names compared case-insensitively, names in other scripts compared exactly, never by substring, and never across simplified/traditional conversion.

A recursive run (`-r/--recursive`) SHALL keep the whole-set behavior it had before this requirement existed: every first-level folder not named by `--ignore-folder` SHALL be input — the whole tree is scanned, no first-level folder is a reference or mirrored — so a nested gallery with no per-character structure organizes exactly as before. Entering incremental mode SHALL be stated in exactly one notice line naming the mode and the number of reference folders, printed before the models load. A non-recursive target directory without first-level subfolders besides the output tree SHALL run exactly as the release before this requirement existed.

Reference folders SHALL be profiled from a sample: up to the fixed sample-size constant of analyzable members per folder, members collected recursively and ordered by content hash, analyzed with the same engines as scanned images and stored in the same content-hash cache, so a re-run pays nothing for an unchanged folder and two runs over the same folder sample the same members. A reference folder whose analyzable sample splits into two or more identity clusters under the same scoring and threshold clustering uses SHALL be demoted: it SHALL NOT capture clusters and SHALL NOT own a character tag, and the report SHALL name it with a hint at `--ingest`. Demotion SHALL NOT change a folder's disposition to input.

An image or cluster matched to a reference folder (by folder match or by character-tag ownership) SHALL be copied into `<directory>/organized/<the reference folder's exact name>/`; names in any character set SHALL be preserved. Skeleton folders SHALL be created lazily: a folder under the output tree SHALL be created only when at least one image is copied into it during the run. New cluster folder names SHALL NOT collide with any existing first-level folder name or output-tree folder name; collisions SHALL receive deterministic numeric suffixes. The run SHALL NOT create, rename, delete, or modify any first-level folder or its contents, and SHALL NOT move or delete the root's loose images.

#### Scenario: Incremental mode entry is announced

- **WHEN** `encro organize <dir>` runs without `-r` and `<dir>` contains first-level subfolders besides `organized/`
- **THEN** exactly one notice line names incremental mode and the number of reference folders, and the run proceeds as an incremental organize

#### Scenario: A recursive run keeps whole-set behavior

- **WHEN** `encro organize <dir> -r` runs over a tree with first-level subfolders such as `角色A/`
- **THEN** those folders' images are input, no first-level folder is a reference or mirrored, and the result matches the recursive release before this requirement existed

#### Scenario: A directory without first-level folders behaves as before

- **WHEN** `encro organize <dir> -r` runs over a directory whose only subfolder is an `organized/` tree
- **THEN** the run is not incremental and behaves exactly as the release before this requirement existed

#### Scenario: A matching cluster files into the mirrored skeleton folder

- **WHEN** a cluster of loose images reaches the identity similarity threshold against the profile of the first-level folder `角色A`
- **THEN** the cluster's images are copied into `organized/角色A/`, the first-level folder `角色A/` itself is unchanged, and the loose originals remain in place

#### Scenario: Zero-match reference folders are listed but not created

- **WHEN** an incremental run completes in which no image matched the reference folder `角色B`
- **THEN** no `organized/角色B/` folder is created and the report lists `角色B` with a zero count and its reference disposition

#### Scenario: A miscellaneous-named folder becomes input

- **WHEN** a first-level folder is named `mix`, `未分类` or `その他` and no flag names it
- **THEN** the folder is not a reference, its images are collected recursively as input, and those images are filed by the same routing as loose images

#### Scenario: The name list matches exactly, never by substring

- **WHEN** first-level folders are named `夏mix子`, `Misc` and `雜項` (with surrounding spaces)
- **THEN** `夏mix子` is a reference, `Misc` is input (ASCII case-insensitive), and `雜項` is input (trimmed, exact traditional-form match)

#### Scenario: Flags override the list and the default

- **WHEN** the run gives `--ingest 角色A` and `--ignore-folder mix`
- **THEN** `角色A` is input despite being a name-shape reference by default, and `mix` is ignored despite being on the list

#### Scenario: A mixed-content reference is demoted with a hint

- **WHEN** a reference folder's sample splits into two identity clusters under the clustering threshold
- **THEN** the folder captures no cluster and owns no character tag, and the report names it as demoted with a hint at `--ingest`

#### Scenario: Sampling goes through the cache

- **WHEN** an incremental run completes and a second run starts over the unchanged directory
- **THEN** no model inference runs for the already-sampled reference members

#### Scenario: A new cluster name never collides with an existing folder

- **WHEN** a new cluster's descriptive name equals the name of an existing first-level folder
- **THEN** the new folder receives a deterministic numeric suffix instead of reusing the name

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

Routing follows a fixed order: assignment by exactly one at-or-above-threshold character tag first, then multi-subject routing to `mixed/`, then clustering of the single-subject remainder. Single-subject images with no character tag at or above the threshold SHALL be clustered by a similarity score that combines two local signals for the image: its identity feature — the fixed-width numeric embedding the identity model produces for the image, normalized to unit length before use, so that its stored magnitude never influences a cluster's shape — and its identity-tag evidence, a unit-length vector built from the image's own identity-bearing general tags, weighted by their confidence and restricted to the tags that describe a person rather than a picture. The two signals SHALL be combined as a weighted mean of their cosines with the fixed weight of 0.8 on the identity feature and 0.2 on the tag evidence (a design constant), and the combined score SHALL NOT depend on any corpus-wide statistic fitted at run time. An image with no identity-tag evidence is compared by its identity-feature cosine alone, against a separately calibrated default of 0.74.

A cluster's profile is the mean of its members' normalized identity features and the mean of their unit identity-tag vectors. Clusters SHALL be formed without requiring the number of clusters in advance and, for a run at or below the recorded image ceiling, without depending on the order in which images were scanned: clustering SHALL repeatedly merge the pair of clusters with the highest mean cross-pair similarity — the average of the combined scores over every pair of members across the two clusters — for as long as some pair reaches the identity similarity threshold, and stop when none does. Above that ceiling (a design constant, so the pairwise scores a merge decision needs stay bounded in memory) the run SHALL fall back to a single greedy pass and SHALL state in its output that it did so: a very large gallery degrades visibly instead of failing or swapping. The identity similarity threshold is a calibrated operating point on the combined score (a design constant, default 0.70, calibrated on labelled character collections; a higher value separates look-alike characters and fragments a character's varied artwork, a lower value does the reverse), and the calibration basis and recipe SHALL be recorded next to the constant so a model or weight change can be re-measured rather than guessed. Images whose combined score to every cluster stays below the threshold stay in their own cluster.

Clusters SHALL be named `unknown_<top-appearance-tags>`, sanitized and length-capped, with deterministic collision suffixes; the tags are read from the cluster members' own general tags, restricted to identity-bearing tags (hair, eyes, anatomy, and signature-accessory patterns; scene and action words describe the picture, not the person, and shall not name a cluster), ranked by how many members carry them. The embedding itself carries no labels, so naming stays tag-derived. A singleton cluster whose file stem matches a `<work>_<index>` download pattern SHALL be grouped with the other singleton clusters of the same `<work>` into one `unknown_source_<work>/` folder when at least two such pages exist (pages of one work share its character cast); pages that match no group and no cluster fall to `uncategorized/`.

#### Scenario: Same original character across styles clusters together

- **WHEN** images of the same original character in differing art styles produce combined scores to the same cluster reaching the identity similarity threshold, and no character tag reaches the threshold
- **THEN** they land in one cluster and one folder

#### Scenario: Similarity below the threshold opens a new cluster

- **WHEN** an image's combined score to every existing cluster is below the identity similarity threshold
- **THEN** the image stays in its own cluster instead of joining the most similar one

#### Scenario: Cluster names describe appearance

- **WHEN** a cluster's members most frequently carry the identity tags `pink_hair` and `blue_eyes`
- **THEN** its folder name contains those tags in the `unknown_` prefix form

#### Scenario: Distinct clusters with identical tag descriptions do not collide

- **WHEN** two distinct clusters reduce to the same descriptive name
- **THEN** deterministic numeric suffixes keep the folders distinct

#### Scenario: Clustering does not depend on input order

- **WHEN** the same collection, at or below the recorded image ceiling, is analysed twice with the images presented in a different order
- **THEN** the resulting clusters contain the same images

#### Scenario: A run above the ceiling reports its fallback

- **WHEN** the number of analysed images exceeds the recorded ceiling
- **THEN** the run states in its output that it fell back to the greedy pass, and every image still lands in exactly one folder

#### Scenario: Tag evidence separates look-alike characters

- **WHEN** two characters have identity features close enough to reach the threshold with each other but disagree in their identity-bearing general tags
- **THEN** the combined score keeps them in separate clusters

#### Scenario: An image without tag evidence is compared by its feature alone

- **WHEN** an image carries no identity-bearing general tag
- **THEN** its similarity is its identity-feature cosine alone, judged against that mode's own calibrated default rather than the combined score's default

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

At the start of every run, existing folders under the output root and first-level reference folders of the target directory (including folders the user renamed, in any character set) SHALL be consulted as naming references: a new cluster whose combined similarity score to a reference folder reaches the identity similarity threshold SHALL be filed into that folder's name instead of receiving an `unknown_` name. A reference folder's profile is the mean of its analyzable members' normalized identity features and the mean of their unit identity-tag vectors, read from cached analysis — for a first-level folder, from the analyses its sample stored in the shared cache (the incremental requirement defines the sampling) — and it is compared profile against profile by the same fixed 0.8/0.2 weighting and the same threshold that clustering uses — the mean-versus-mean comparison of the teaching path, which is not the member-pair mean clustering merges on; a reference folder whose members hold identity features but no identity-tag evidence is compared on its mean feature alone against that mode's calibrated default. A first-level folder and an output-root folder of the same name SHALL be one reference whose membership is the union of both sources. A folder with no analyzable member SHALL be skipped as a reference, a folder whose members hold no cached identity feature SHALL be skipped as a folder-match reference — its tags may still own a character tag — and a demoted folder (mixed sample, per the incremental requirement) SHALL be skipped entirely, as folder-match reference and as tag owner. Additionally, character-tag assignment SHALL respect folder ownership: when a reference folder's contents identify with a character tag (that tag being carried as the sole at-or-above-threshold character tag by a majority of the folder's analyzable members), images assigned that character tag SHALL be filed into the owning folder's current name instead of the sanitized tag name; when several folders claim the same tag, the folder with the most tagged members wins. encro SHALL never rename, delete, or reorganize existing folders, and SHALL never modify a first-level folder or its contents. User-renamed folders SHALL take precedence over both `unknown_` naming and raw tag-derived names.

#### Scenario: Renamed cluster folder teaches the next run

- **WHEN** a previous run produced `unknown_pink_hair_blue_eyes/`, the user renamed it to `我的角色`, and a later run finds a matching cluster
- **THEN** the new cluster's images are copied into `我的角色`

#### Scenario: A first-level folder teaches the run

- **WHEN** an incremental run finds a cluster whose profile reaches the identity similarity threshold against the sampled profile of the first-level folder `温迪`
- **THEN** the cluster's images are copied into `organized/温迪/` and the first-level `温迪/` folder is unchanged

#### Scenario: A first-level folder owns its character tag

- **WHEN** the sampled members of the first-level folder `温迪` mostly carry `hatsune_miku` as their sole confident character tag, and a loose image is assigned `hatsune_miku`
- **THEN** the image is copied into `organized/温迪/` and no `hatsune_miku` folder is created

#### Scenario: Same-name sources merge into one reference

- **WHEN** the target directory holds both `角色A/` at the first level and `organized/角色A/` from an earlier run
- **THEN** they form one reference whose membership is the union of both sources, under that one name

#### Scenario: A dissimilar folder does not capture a cluster

- **WHEN** a run finds clusters and existing folders, but no reference folder reaches the identity similarity threshold against a cluster
- **THEN** that cluster keeps an `unknown_` name instead of being filed into a folder

#### Scenario: Renamed character folder teaches the next run

- **WHEN** a previous run produced `hatsune_miku/`, the user renamed it to `初音ミク`, and a later run analyzes images tagged `hatsune_miku`
- **THEN** those images are copied into `初音ミク` and no `hatsune_miku` folder is recreated

#### Scenario: Existing folders are never modified

- **WHEN** a run completes with reference folders present
- **THEN** every pre-existing folder still exists under its original name and its previous contents are unchanged

#### Scenario: A look-alike folder does not capture a cluster

- **WHEN** a cluster's identity feature is close to a reference folder's mean feature, but the members' identity-tag evidence disagrees with the folder's
- **THEN** the combined score keeps the cluster out of that folder

### Requirement: Same-run folder references

Folder matching SHALL use this run's own character-tag assignment as a second source of references, so that the first run over a gallery can file clusters into the folders that same run created. The images this run assigned to one character folder — by character tag, or because an existing folder owns that tag — SHALL form a reference for that folder; `mixed/` and `uncategorized/` SHALL NOT become references, because a group of images that is not a single character cannot teach one. A reference's profile is the mean of its members' normalized identity features and the mean of their unit identity-tag vectors, and it is compared under the same rules the teaching requirement applies to existing folders — the same weighted combination, the same separately calibrated default when a side carries no identity-tag evidence, and the same threshold — so that a cluster reaching that threshold against it is filed into that folder's name. A routing-derived reference and an on-disk folder of the same name SHALL be one reference whose membership is the union of both sources. An on-disk folder SHALL take precedence where the two disagree: images assigned a character tag that an existing folder owns are filed into the owning folder's current name, and a routing-derived reference SHALL NOT cause an existing output folder to be renamed or deleted, nor change the contents it already holds. Routing-derived references SHALL be deterministic — they depend only on the run's analyses and routing decisions, not on filesystem iteration order. A run whose routing assigns no image to a character folder SHALL behave exactly as it did before this requirement existed.

#### Scenario: A first run files a cluster into the folder its own routing created

- **WHEN** a run scans a gallery with no output folders yet, one image's character tag `hatsune_miku` files it under `hatsune_miku`, and a cluster's profile reaches the identity similarity threshold against the profile of the images filed there
- **THEN** that cluster's images are copied into `hatsune_miku` in the same run instead of receiving an `unknown_` name

#### Scenario: A renamed folder keeps owning its character tag

- **WHEN** a previous run produced `hatsune_miku/`, the user renamed it to `初音ミク`, and the current run assigns images to `hatsune_miku` by character tag
- **THEN** those images and any cluster matching their profile are filed into `初音ミク`, and no `hatsune_miku` folder appears

#### Scenario: A routing-derived reference captures nothing below the threshold

- **WHEN** a cluster's profile stays below the identity similarity threshold against every reference, including the references this run's routing produced
- **THEN** the cluster keeps an `unknown_` name instead of being filed into a folder

#### Scenario: A run that routes no character behaves as before

- **WHEN** routing assigns no image to any character folder (everything is `mixed/` or unassigned)
- **THEN** clustering, cluster naming and folder matching produce the same result as the release before this requirement existed
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

Analysis results SHALL be cached keyed by SHA-256 of file content under `<directory>/organized/.cache/`, and SHALL hold everything a later run needs to skip re-analysis: the identity feature and the tag pairs. Re-runs SHALL skip analysis for unchanged images (renames and moves still hit the cache). The cache SHALL be persisted in bounded batches (not one rewrite per image) and flushed at the analysis stage boundary and on interruption, so an interrupted run (including Ctrl-C) resumes without redoing completed analysis beyond the in-flight batch. Stored tag pairs SHALL be limited to each category's consuming threshold (general at or above the naming floor, character at or above the weakest routing threshold) so identity noise cannot dominate the store. A cache written by a different cache format version SHALL be treated as empty, so an upgrade re-analyzes every image once instead of reading entries that lack the identity feature. The format version SHALL be raised whenever the stored analysis changes meaning, not only when the entry shape changes: a model replacement or a change in how the model input is prepared makes every stored feature and tag pair stale, and a cache that mixed entries from two preparations would compare values that are not comparable. `--recluster` SHALL discard cached analysis before running. Cached rating tags SHALL never influence folder assignment.

#### Scenario: Re-run does not re-analyze

- **WHEN** a run completes and the same directory is scanned again with no changes
- **THEN** no model inference runs for the already-cached images

#### Scenario: Interrupted run resumes

- **WHEN** a run is interrupted mid-analysis and re-run
- **THEN** analysis continues from the cache and previously analyzed images are not re-processed

#### Scenario: Cache from an earlier format is discarded

- **WHEN** a cache written by an earlier cache format version is present at the start of a run
- **THEN** the run analyzes every image again, and the file is rewritten as the new entries are stored

#### Scenario: A change in how the model input is prepared invalidates stored features

- **WHEN** a release changes the preparation of the model input, and a cache written by the previous release is present at the start of a run
- **THEN** the run analyzes every image again instead of reading features computed from the previous preparation

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

### Requirement: Identity model input preparation

The identity feature SHALL be computed from input prepared exactly the way the identity model's reference implementation prepares it: the image resampled to the model's input edge with the reference implementation's resampling filter (aspect ratio not preserved), pixel values scaled to the 0..1 range, and then normalized per channel with the model's documented channel mean and standard deviation, delivered in the model's documented tensor layout. No preparation step SHALL alter those pixels in any other way — no further scaling, colour transform, masking or padding — but the decode and the resampling are the platform's own, so agreement with a reference extraction is bounded by the two implementations' last-bit differences rather than being bit-exact. The feature SHALL remain stored as a unit-length vector. The input contract that belongs to the pinned model file (edge size, resampling filter, scaling, normalization, layout) SHALL be stated next to that pin, so that replacing the model file requires restating its contract instead of inheriting the previous model's preparation.

#### Scenario: Input matches the model's reference preprocessing

- **WHEN** a frame with known pixel values passes through the identity input preparation
- **THEN** the frame is resampled to the model's input edge with the reference resampling filter, each channel is scaled to 0..1 and then normalized with that channel's documented mean and standard deviation, and the values reach the model in the documented layout

#### Scenario: The input contract is recorded beside the model pin

- **WHEN** the pinned identity model file and the input contract recorded with it are read together
- **THEN** the record names the input edge, the resampling filter, the pixel scaling, the per-channel normalization and the tensor layout the conversion implements, so a replacement cannot inherit them silently

### Requirement: Execution provider selection

The analyzer SHALL attempt the CUDA execution provider first and fall back to the CPU provider when CUDA initialization fails, printing exactly one notice line naming the active provider. Provider selection SHALL never fail a run by itself.

#### Scenario: CUDA unavailable falls back with a notice

- **WHEN** the CUDA runtime DLLs are absent or initialization fails
- **THEN** the run proceeds on the CPU provider and prints one notice naming the CPU provider

#### Scenario: Active provider is reported

- **WHEN** a run starts
- **THEN** exactly one line names the execution provider in use

### Requirement: Progress and report

During analysis the command SHALL show a progress bar with completion count, image rate, and ETA on a TTY; no progress bar SHALL be rendered when stdout is not a terminal. Reference sampling in an incremental run SHALL show its own progress bar under the same rules. While the tagger and identity engines load, the command SHALL show an indeterminate spinner on a TTY and print no provider notice until the engines are built, so exactly one provider notice follows the spinner. After execution the command SHALL print a report: per-folder counts, each folder's assignment source (character tag, folder match, new cluster, `mixed/`, `uncategorized/`), and run totals. In an incremental run the report SHALL additionally list every first-level reference folder — including those that matched no image, shown with a zero count and their disposition — and a disposition summary SHALL name each first-level folder as reference, input, ignored, or demoted, with demoted folders carrying a hint at `--ingest`. The report's folder table SHALL keep its columns aligned: the folder-column width SHALL be derived from the folder names present and the terminal width — never below a fixed minimum, and never beyond the terminal budget when that budget exceeds the minimum (a narrower terminal keeps the minimum, matching the fixed layout) — and every column SHALL be padded to a consistent display width. A folder name wider than the derived column SHALL be truncated with an ellipsis rather than pushing the count and source columns out of alignment.

#### Scenario: Analysis shows progress with rate and ETA

- **WHEN** analysis runs on a TTY with more than a few images
- **THEN** a progress bar shows images completed, images per second, and an ETA

#### Scenario: Reference sampling shows its own progress

- **WHEN** an incremental run samples reference folders that need analysis on a TTY
- **THEN** a progress bar covers the sampled images, separately from the loose-image analysis bar

#### Scenario: Engine loading shows a spinner

- **WHEN** the command starts with real (non-fake) engines on a TTY and the model files are being loaded
- **THEN** an indeterminate spinner is shown while the engines load
- **AND** exactly one provider notice is printed after the spinner is cleared

#### Scenario: Report lists folders with sources

- **WHEN** a run completes with folders from character tags, folder matches, and new clusters
- **THEN** the report lists each folder with its image count and assignment source

#### Scenario: Incremental report lists unmatched references and dispositions

- **WHEN** an incremental run completes with a zero-match reference folder and a demoted folder
- **THEN** the report lists the zero-match folder with a zero count and its disposition, and the disposition summary names the demoted folder with a hint at `--ingest`

#### Scenario: Long folder names stay aligned

- **WHEN** a run completes with a folder name wider than the table's folder column
- **THEN** that folder name is truncated with a trailing ellipsis in the report
- **AND** the images and source columns of every row, including that row, align with the header

#### Scenario: Narrow tables stay compact

- **WHEN** a run completes where every folder name fits the table's minimum folder-column width
- **THEN** the folder column stays at that minimum width instead of growing toward the terminal width

### Requirement: Copy failures are named in the report

The end-of-run report SHALL list every image whose copy failed, naming the source path and the destination path, after the run totals and in the report's indented detail-line style. The run SHALL report what was actually copied in its totals, and a failed copy SHALL NOT change the run's exit code: the exit code SHALL be the one the run would end with otherwise (0 for a completed run), and a re-run SHALL retry exactly the copies that are missing.

#### Scenario: A failed copy is named in the report

- **WHEN** a run completes with an image whose copy failed (for example the destination tree cannot be written)
- **THEN** the report lists that image's source and destination after the totals line
- **AND** the run exits with the exit code it would otherwise have (0)

### Requirement: Dry run

`--dry-run` SHALL run the full analysis and print the assignment plan (the report) without copying anything and without creating output folders beyond the cache.

#### Scenario: Dry run copies nothing

- **WHEN** `encro organize <dir> --dry-run` runs
- **THEN** the report is printed, no `organized/<name>/` folders are created, and no images are copied
