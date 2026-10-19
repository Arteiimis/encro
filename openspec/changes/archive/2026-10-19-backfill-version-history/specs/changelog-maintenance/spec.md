# Spec Delta

## Purpose

Defines what `CHANGELOG.md` must contain and how a released tag relates to it, so a reader can tell what changed between any two versions without reading commit history.

## ADDED Requirements

### Requirement: Changelog file and grouping

The repository SHALL keep `CHANGELOG.md` at its root, following Keep a Changelog 1.1.0: a header naming the format and Semantic Versioning, then per-version sections, with entries grouped under exactly the types `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed` and `Security`.

#### Scenario: A section uses the fixed type names

- **WHEN** a version's section records new functionality and a bug fix
- **THEN** they appear under `### Added` and `### Fixed` respectively, and no other type heading is invented

#### Scenario: An empty group is omitted

- **WHEN** a version removed nothing
- **THEN** its section carries no `### Removed` heading

### Requirement: One section per version with its date

`CHANGELOG.md` SHALL contain one section per released version, headed `## [x.y.z] - YYYY-MM-DD` with an ISO 8601 date, ordered newest first.

#### Scenario: Every tag has a section

- **WHEN** the repository carries N release tags
- **THEN** `CHANGELOG.md` contains N version sections whose versions equal the tag names without the `v` prefix

#### Scenario: Order is newest first

- **WHEN** a reader opens `CHANGELOG.md`
- **THEN** the highest version's section appears above every lower one, and dates do not increase down the file (two versions released on one day share a date)

### Requirement: Unreleased and yanked markers

`CHANGELOG.md` SHALL keep a `## [Unreleased]` section at the top for changes not yet released, and SHALL mark a withdrawn version `## [x.y.z] - YYYY-MM-DD [YANKED]`.

#### Scenario: Unreleased heads the file

- **WHEN** work has landed but no version carries it yet
- **THEN** those entries sit under `## [Unreleased]`, above the newest released section

#### Scenario: A withdrawn version is marked, not deleted

- **WHEN** a released version must be withdrawn
- **THEN** its section stays in place with `[YANKED]` appended to its heading

### Requirement: Linkable versions

`CHANGELOG.md` SHALL end with a reference block giving every version heading a link: a compare URL against the previous version for each released version, a compare-to-HEAD URL for `[Unreleased]`, and the release page for the first version.

#### Scenario: Two adjacent versions are diffable

- **WHEN** a reader clicks the link on a version heading
- **THEN** it resolves to a diff between that version and its predecessor

#### Scenario: The first version links to its release

- **WHEN** the lowest version has no predecessor
- **THEN** its link resolves to that tag's release page rather than a compare URL

### Requirement: A tag's tree carries its own section

Checking out a release tag SHALL show a `CHANGELOG.md` whose newest version section is that tag's version.

#### Scenario: Checkout matches the version

- **WHEN** a reader checks out the tag for version `2.0.0-alpha.3`
- **THEN** `CHANGELOG.md` in that tree has `2.0.0-alpha.3` as its newest version section and contains no later version

#### Scenario: The head of history holds every section

- **WHEN** a reader opens `CHANGELOG.md` on the current branch
- **THEN** every version section exists, including those whose tags are ancestors of it

### Requirement: Entries are curated, not dumped

Each changelog entry SHALL be one human-readable statement of a notable change, written for a reader of the software; the file MUST NOT be a formatted commit log, and routine commits — merges, formatting, internal refactors, documentation-only changes — MUST NOT appear.

#### Scenario: A notable change reads as prose

- **WHEN** a version added a capability
- **THEN** its `### Added` entry states what a user can now do, not the commits that implemented it

#### Scenario: Noise stays out

- **WHEN** the range between two versions contains merge commits, formatting passes and documentation edits
- **THEN** none of them appears as an entry
