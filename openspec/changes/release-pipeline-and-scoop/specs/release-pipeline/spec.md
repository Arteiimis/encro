# Spec Delta

## Purpose

Turns a version tag into something a stranger can download and run: one archive per platform carrying the runtime it needs, published only when the repository is green, described by that version's changelog entry.

## ADDED Requirements

### Requirement: Each distributed release publishes one artifact per platform

A release that distributes binaries SHALL publish one runtime archive per platform: one for Windows and one for Linux. Each platform SHALL contribute only its own archive format. Releases recorded before the project distributed anything are governed by the historical-release requirement instead.

#### Scenario: A published release carries both runtime artifacts

- **WHEN** a distributed release for version 2.0.1 is published
- **THEN** it carries exactly one Windows runtime archive and one Linux runtime archive

#### Scenario: No other archive kinds are published

- **WHEN** a distributed release is published
- **THEN** it carries no installer, no source archive and no checksum-only companion file
- **AND** every asset is either a runtime archive or a symbol archive

#### Scenario: Each platform contributes one runtime file, not one per format

- **WHEN** a distributed release is published
- **THEN** the Windows runtime archive is a zip and the Linux one is a tarball
- **AND** neither platform contributes both formats

### Requirement: Debug symbols ship as their own archive

A release SHALL publish the debug symbols that build produced as a separate downloadable archive, so that a crash trace can be symbolised without downloading them as part of the run. A symbol archive SHALL NOT be merged into a runtime archive, and the platform whose crash traces need symbols SHALL have one available for every distributed version.

#### Scenario: Symbols are published separately from the run

- **WHEN** a distributed release is published
- **THEN** the platform whose crash traces need symbols has its symbols in a separate downloadable archive
- **AND** the runtime archive does not contain them

#### Scenario: The symbols that build produced are obtainable

- **WHEN** a user has a crash trace from a published build and wants it symbolised
- **THEN** the symbols for that exact version are downloadable from the same release
- **AND** placing them beside the executable is enough to resolve application frames

### Requirement: An artifact name identifies its version, architecture and platform

Each artifact name SHALL contain the release version, the CPU architecture and the platform, so a version can be matched to its assets without opening them. The version SHALL appear without the tag's `v` prefix, and the architecture token SHALL be the one that platform's own toolchain reports rather than a value normalised across platforms.

#### Scenario: The asset name drops the tag prefix

- **WHEN** the published tag is `v2.0.1`
- **THEN** the artifact names contain `2.0.1`, never `v2.0.1`

#### Scenario: Each platform names its own architecture

- **WHEN** both artifacts of one release are listed
- **THEN** each name states the architecture that platform's toolchain reports
- **AND** the two names are not forced to spelling the architecture identically

### Requirement: A version that was never distributed is recorded without artifacts

Every tag that predates the project's distribution SHALL have a release entry once its history is completed, and that entry SHALL carry no downloadable artifact. Its text SHALL state that the version was never distributed and that the entry records history rather than announcing a download. A tag this pipeline attempted to publish gets an entry only once that attempt succeeds, so a failed attempt stays visible as a missing entry rather than being reclassified as history.

#### Scenario: An early version has an entry but nothing to download

- **WHEN** the release entry for a version that predates distribution is opened
- **THEN** its text is that version's changelog section prepended with a statement that it was never distributed
- **AND** it has no downloadable assets

#### Scenario: The first distributed version says so

- **WHEN** the first version that actually ships binaries is opened alongside the earlier entries
- **THEN** its entry carries both artifacts
- **AND** the difference between it and the earlier entries is visible from the entries themselves

#### Scenario: Every tag that predates the pipeline has exactly one entry

- **WHEN** the repository's tags below the backfill boundary are compared with its release entries
- **THEN** every such tag has one entry and no entry lacks a tag
- **AND** a tag whose publish attempt failed is the only kind that may lack one

### Requirement: The Windows artifact starts on a machine that has never seen encro

The Windows artifact SHALL contain the executable and every library the executable loads at process start other than the operating system's own runtime libraries. Unpacking it into an empty directory and running it SHALL produce working output without any further download or installation beyond the documented external prerequisites.

#### Scenario: The archive alone is enough to run

- **WHEN** the Windows artifact is unpacked into an empty directory and the executable is run there
- **THEN** it starts and reports its version
- **AND** it does not fail on a missing library

#### Scenario: Libraries loaded only on demand stay out

- **WHEN** the Windows artifact is inspected
- **THEN** it contains the startup library the executable imports
- **AND** it contains no GPU or provider libraries that are only loaded when an optional execution provider is requested

### Requirement: A package carries no build-time residue

A package SHALL contain the files a user needs to run the tool and nothing that exists only to build or debug it. Debug symbols, object files and intermediate output SHALL NOT be packaged; the symbols are published as their own archive beside the runtime archive, so a user who needs symbolised crash traces can obtain exactly the symbols that build produced.

#### Scenario: Debug symbols are not inside the runtime archive

- **WHEN** either runtime archive is unpacked
- **THEN** it contains no debug-symbol file
- **AND** the executable is still present and runnable

#### Scenario: The package is close to the size of what it runs

- **WHEN** the Windows artifact's contents and its compressed size are compared
- **THEN** the compressed size is accounted for by the executable, its startup libraries, the licence and the README
- **AND** no single file dwarfs the executable without being needed at run time

### Requirement: The repository documents how to install the tool

The repository's README SHALL state how to install the tool on each distributed platform, and SHALL distinguish the versions that can be downloaded from the entries that only record history. It SHALL name the external programs required and the platform-specific capability limits that a reader must know before installing.

#### Scenario: A reader can install from the README alone

- **WHEN** a reader who has never used the tool follows the README's installation section on a clean machine
- **THEN** they end up with a working `encro` command
- **AND** they did not need to consult any other document

#### Scenario: The Releases page is explained

- **WHEN** the README describes where downloads come from
- **THEN** it states that earlier entries are recorded history without downloads
- **AND** a reader does not conclude that those versions are obtainable

### Requirement: Each artifact states what its platform can and cannot do

Every distributed artifact SHALL contain the licence and a README. The README SHALL state, for the platform the artifact targets, which subcommands are available and which are not, and SHALL name the external programs the tool invokes but does not ship.

#### Scenario: Linux limits are stated before the user hits them

- **WHEN** the Linux artifact is unpacked
- **THEN** its README states that character organization is unavailable on that platform
- **AND** a user reading only the archive learns this without running the tool

#### Scenario: The external prerequisite is named

- **WHEN** either artifact is unpacked
- **THEN** its README names the media programs that must already be installed

#### Scenario: The licence travels with the binary

- **WHEN** either artifact is unpacked
- **THEN** a licence file is present at its root

### Requirement: Nothing is published unless the repository is green

A release SHALL be withheld unless three checks pass first: the complete test suite (unit and end-to-end), the tag-contract check that every tag still satisfies its naming, ordering and changelog obligations, and an agreement check confirming that the version the build will stamp equals the version the tag names. A failing check SHALL abort the release before any artifact becomes downloadable.

#### Scenario: A failing test publishes nothing

- **WHEN** the test suite fails for the tagged commit
- **THEN** no release is created and no artifact is downloadable

#### Scenario: A broken tag line publishes nothing

- **WHEN** the tag-contract check fails
- **THEN** the release is aborted even though the tests passed

#### Scenario: A tag that does not match the built version publishes nothing

- **WHEN** the tag names a version different from the one the build will stamp into the binary and its artifacts
- **THEN** the release is aborted before packaging
- **AND** the mismatch is reported rather than resolved by rewriting either side

#### Scenario: All checks pass and the release proceeds

- **WHEN** the tests, the tag contract and the version agreement all pass
- **THEN** the release is created with both artifacts attached

### Requirement: A new tag publishes automatically

Pushing a tag that names a version SHALL publish that version without further input, provided the tagged source can produce a working package.

#### Scenario: Pushing a new tag publishes it

- **WHEN** a maintainer pushes the tag `v2.0.1`
- **THEN** a release for `v2.0.1` is published without any further action

#### Scenario: A tag that is not a version is ignored

- **WHEN** a pushed tag does not name a version
- **THEN** no release is attempted

### Requirement: An existing tag can be published on demand

A maintainer SHALL be able to publish a tag that already exists and was pushed before this pipeline existed by naming that tag explicitly, with the same result a fresh push would give. A tag whose version predates the first version this pipeline can package SHALL be refused, with the reason stated — the decision is a version comparison against a recorded boundary, not a guess about whether that tree happens to build.

#### Scenario: A tag that predates the pipeline is published on demand

- **WHEN** a maintainer names an already-existing tag whose source can produce a working package
- **THEN** that version is published exactly as a newly pushed tag would be

#### Scenario: A tag that predates the packaging fix is refused

- **WHEN** a maintainer names an already-existing tag whose version is below the recorded boundary
- **THEN** the run refuses before packaging anything
- **AND** it names that reason rather than publishing a package known to be broken

### Requirement: A history that predates the pipeline can be completed in one action

A single entry point SHALL record every existing tag up to and including a version the maintainer names, limited to those that have no release entry. The named version is the boundary: tags above it are never touched, so a version whose publish failed cannot be reclassified as history. The entry point SHALL record all qualifying tags in one run rather than one run per version.

#### Scenario: Completing the history takes one action

- **WHEN** a maintainer asks for the missing entries up to the latest version that predates the pipeline
- **THEN** every tag at or below that version without an entry gains one in the same run
- **AND** the versions that were never distributed gain entries without artifacts

#### Scenario: A version above the boundary is left alone

- **WHEN** a publish attempt for a version failed and the same completion action is run afterwards with the previous boundary
- **THEN** that version is not recorded as never distributed
- **AND** it remains without an entry so the publish can be retried

### Requirement: The release body is that version's changelog entry

The text of a published release SHALL be the changelog section for that exact version — the same text a reader finds under that version's heading in the repository's changelog — and SHALL NOT be a commit list or a generated summary of the diff.

#### Scenario: The body matches the changelog section

- **WHEN** the release for a version is opened
- **THEN** its body is that version's changelog section
- **AND** it does not list raw commits

#### Scenario: A version with no changelog section is not published

- **WHEN** a tag exists but the changelog has no section for its version
- **THEN** the release is aborted rather than published with an empty or invented body

#### Scenario: Historical entries use the same body

- **WHEN** a historical version's entry is opened
- **THEN** its body is that version's changelog section, written the same way as a distributed version's body
- **AND** only the added statement about never being distributed distinguishes it

### Requirement: Publishing a version twice changes nothing

A version SHALL be publishable at most once. Re-running the release for a version that already has a release SHALL leave the existing release and its artifacts untouched rather than replacing them, and a published tag SHALL never be moved to a different commit.

#### Scenario: A repeated run is a no-op

- **WHEN** the release for a version is triggered again after it was published
- **THEN** the existing release and its artifact files are unchanged
- **AND** the run reports that the version was already published

#### Scenario: Artifacts are not overwritten silently

- **WHEN** a repeated run produces artifacts whose contents differ from the published ones
- **THEN** the published assets are still not replaced
- **AND** the difference is reported rather than resolved automatically
