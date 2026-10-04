## ADDED Requirements

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

## MODIFIED Requirements

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
