## MODIFIED Requirements

### Requirement: Command surface and scanning

`encro organize <directory>` SHALL scan the directory for still-image files (`.jpg`, `.jpeg`, `.png`, `.webp`), skipping the `organized/` output tree itself. The set is matched case-insensitively (`media-scan`), so uppercase and mixed-case spellings of these extensions are scanned too. Scanning SHALL be non-recursive by default and recursive with `-r/--recursive`; the `recursive` config key applies with the standard CLI > config > default precedence, and the built-in default is non-recursive. An empty scan result SHALL exit 0 with a message stating that no images were found. A nonexistent or unreadable target directory SHALL exit non-zero with an error naming the path. The subcommand help SHALL state that analysis is local and that images are not uploaded.

The directory SHALL be optional when `--download-models` is given: in that form the command provisions the model files, reports the model directory it provisioned, and exits 0 without scanning and without copying anything, because fetching the models needs no images. With a directory, `--download-models` keeps fetching what is missing before the run, and the model-presence check applies to both forms. Without a directory and without `--download-models`, the command SHALL exit non-zero with the argument error naming the directory and the help hint. The subcommand help SHALL show the directory as optional in its usage and SHALL state, for `--download-models`, the approximate download size and that the models are fetched and the run stops there when no directory is given.

`--identity-tau <value>` SHALL set the identity similarity threshold that decides whether an image or a cluster belongs with a cluster or an existing folder, replacing the built-in calibrated default; the `identity-tau` config key applies with the standard CLI > config > default precedence. The value SHALL be validated to the open interval (0, 1] and an out-of-range value SHALL be rejected as an argument error before any scan. The subcommand help SHALL name the option, state its built-in default and state that a higher value keeps look-alike characters apart while a lower one keeps a character's varied artwork together.

#### Scenario: Default scan excludes subdirectories and output tree

- **WHEN** the target directory contains images directly, images in a subdirectory, and an `organized/` tree from a previous run
- **THEN** only the images directly in the target directory are processed

#### Scenario: Uppercase image extensions are scanned

- **WHEN** the target directory contains an image named `PHOTO.JPG`
- **THEN** the image is scanned and processed like its lowercase spelling

#### Scenario: Recursive flag includes subdirectories

- **WHEN** `encro organize <dir> -r` runs over a tree that also contains an `organized/` output tree
- **THEN** images under subdirectories are processed and the `organized/` tree is still excluded

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

### Requirement: Teaching by renamed folders

At the start of every run, existing folders under the output root (including folders the user renamed, in any character set) SHALL be consulted as naming references: a new cluster whose combined similarity score to a reference folder reaches the identity similarity threshold SHALL be filed into that folder's name instead of receiving an `unknown_` name. A reference folder's profile is the mean of its analyzable members' normalized identity features and the mean of their unit identity-tag vectors, read from cached analysis, and it is compared profile against profile by the same fixed 0.8/0.2 weighting and the same threshold that clustering uses — the mean-versus-mean comparison of the teaching path, which is not the member-pair mean clustering merges on; a reference folder whose members hold identity features but no identity-tag evidence is compared on its mean feature alone against that mode's calibrated default. Additionally, character-tag assignment SHALL respect folder ownership: when a reference folder's contents identify with a character tag (that tag being carried as the sole at-or-above-threshold character tag by a majority of the folder's analyzable members), images assigned that character tag SHALL be filed into the owning folder's current name instead of the sanitized tag name; when several folders claim the same tag, the folder with the most tagged members wins. Folders containing no analyzable member files SHALL be skipped as references, and a folder whose members hold no cached identity feature SHALL be skipped as a folder-match reference — its tags may still own a character tag. encro SHALL never rename, delete, or reorganize existing output folders. User-renamed folders SHALL take precedence over both `unknown_` naming and raw tag-derived names.

#### Scenario: Renamed cluster folder teaches the next run

- **WHEN** a previous run produced `unknown_pink_hair_blue_eyes/`, the user renamed it to `我的角色`, and a later run finds a matching cluster
- **THEN** the new cluster's images are copied into `我的角色`

#### Scenario: A dissimilar folder does not capture a cluster

- **WHEN** a run finds clusters and existing folders, but no reference folder reaches the identity similarity threshold against a cluster
- **THEN** that cluster keeps an `unknown_` name instead of being filed into a folder

#### Scenario: Renamed character folder teaches the next run

- **WHEN** a previous run produced `hatsune_miku/` from character tags, the user renamed it to `初音ミク`, and a later run analyzes images tagged `hatsune_miku`
- **THEN** those images are copied into `初音ミク` and no `hatsune_miku` folder is recreated

#### Scenario: Existing folders are never modified

- **WHEN** a run completes with reference folders present
- **THEN** every pre-existing folder still exists under its original name and its previous contents are unchanged

#### Scenario: A look-alike folder does not capture a cluster

- **WHEN** a cluster's identity feature is close to a reference folder's mean feature, but the members' identity-tag evidence disagrees with the folder's
- **THEN** the combined score keeps the cluster out of that folder
