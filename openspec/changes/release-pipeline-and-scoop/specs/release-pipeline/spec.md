# Spec Delta

## Purpose

Turns a version tag into something a stranger can download and run: one archive per platform carrying the runtime it needs, published only when the repository is green, described by that version's changelog entry.

## ADDED Requirements

### Requirement: Each distributed release publishes one artifact per platform

A release that distributes binaries SHALL publish exactly two downloadable artifacts: one for Windows and one for Linux. Each artifact name SHALL identify the release version, the CPU architecture and the platform, so that a version can be matched to its assets without opening them, and the version in the name SHALL appear without the tag's `v` prefix. Releases recorded before the project distributed anything are governed by the historical-release requirement instead.

#### Scenario: A published release carries both artifacts

- **WHEN** a release for version 2.0.1 is published
- **THEN** it carries exactly one Windows artifact and one Linux artifact
- **AND** each name contains `2.0.1`, the architecture and the platform

#### Scenario: The asset name drops the tag prefix

- **WHEN** the published tag is `v2.0.1`
- **THEN** the artifacts are named `encro-2.0.1-...`, never `encro-v2.0.1-...`

#### Scenario: No other archive kinds are published

- **WHEN** a distributed release is published
- **THEN** it carries no installer, no source archive and no checksum-only companion file
- **AND** the number of assets equals the number of platforms

### Requirement: A version that was never distributed is recorded without artifacts

Every tag SHALL have a release entry, including the versions that predate the project's distribution. Such an entry SHALL carry no downloadable artifact, and its text SHALL state that the version was never distributed and that the entry records history rather than announcing a download. This keeps the release history aligned with the tag history without implying that those versions were ever available.

#### Scenario: An early version has an entry but nothing to download

- **WHEN** the release entry for a version that predates distribution is opened
- **THEN** its text is that version's changelog section prepended with a statement that it was never distributed
- **AND** it has no downloadable assets

#### Scenario: The first distributed version says so

- **WHEN** the first version that actually ships binaries is opened alongside the earlier entries
- **THEN** its entry carries both artifacts
- **AND** the difference between it and the earlier entries is visible from the entries themselves

#### Scenario: Every tag has exactly one entry

- **WHEN** the repository's tags and its release entries are compared
- **THEN** every tag has one entry and no entry lacks a tag

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

A release SHALL be withheld unless two checks pass first: the complete test suite (unit and end-to-end) and the tag-contract check that every tag still satisfies its naming, ordering and changelog obligations. A failing check SHALL abort the release before any artifact becomes downloadable.

#### Scenario: A failing test publishes nothing

- **WHEN** the test suite fails for the tagged commit
- **THEN** no release is created and no artifact is downloadable

#### Scenario: A broken tag line publishes nothing

- **WHEN** the tag-contract check fails
- **THEN** the release is aborted even though the tests passed

#### Scenario: Both checks pass and the release proceeds

- **WHEN** the test suite and the tag-contract check both pass
- **THEN** the release is created with both artifacts attached

### Requirement: A new tag publishes automatically, and existing tags can be published on demand

Pushing a tag that names a version SHALL publish that version without further input. Independently, a maintainer SHALL be able to publish a tag that already exists and was pushed before this behaviour existed — starting with the first distributed release — by naming that tag explicitly. A single further entry point SHALL record every existing tag that has no release entry, so the history can be completed in one action rather than one run per version.

#### Scenario: Pushing a new tag publishes it

- **WHEN** a maintainer pushes the tag `v2.0.1`
- **THEN** a release for `v2.0.1` is published without any further action

#### Scenario: A tag that predates the pipeline is published on demand

- **WHEN** a maintainer names an already-existing tag such as `v2.0.0`
- **THEN** that version is published exactly as a newly pushed tag would be

#### Scenario: Completing the history takes one action

- **WHEN** a maintainer asks for the missing release entries to be recorded
- **THEN** every tag without an entry gains one in the same run
- **AND** the versions that were never distributed gain entries without artifacts

#### Scenario: A tag that is not a version is ignored

- **WHEN** a pushed tag does not name a version
- **THEN** no release is attempted

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
