# Spec Delta

## Purpose

Defines how encro names and orders its versions, and which of the project's surfaces semver judges, so that a tag, the binary's `--version` output, a changelog entry and a package manifest can never disagree.

## ADDED Requirements

### Requirement: Public API declaration

The repository SHALL maintain `docs/public-api.md`, enumerating every surface semver obligations are judged against: CLI subcommands, flag names, the value sets each flag accepts, process exit codes, `config.json` keys and value types, the default job-state file path and its persisted format, and the `--log-json` field names.

#### Scenario: A change is classified against a written surface

- **WHEN** a proposed change alters any surface
- **THEN** the declaration names that surface, and its version increment follows from the rule the declaration maps to it

#### Scenario: An undeclared surface cannot silently become contract

- **WHEN** a change alters behavior the declaration does not name
- **THEN** the change is not a breaking change, and adding that behavior to the declaration is a separate, deliberate edit

### Requirement: Version string source of truth

The version printed by `encro --version` SHALL be generated at build time from the project's declared version, not written as a literal in source.

#### Scenario: The binary reports the declared version

- **WHEN** the project declares version `2.0.0` and the binary is built
- **THEN** `encro --version` reports `2.0.0` and no other version string appears in its output

#### Scenario: A stale literal cannot survive

- **WHEN** the declared version changes and the binary is rebuilt
- **THEN** `encro --version` reports the new value without any source edit outside the declaration

### Requirement: Tag naming grammar

Every release tag SHALL be the letter `v` followed by a version conforming to the semver 2.0.0 grammar: `MAJOR.MINOR.PATCH`, optionally followed by `-` and dot-separated pre-release identifiers, and optionally by `+` and dot-separated build identifiers.

#### Scenario: A conforming tag is accepted

- **WHEN** a tag reads `v2.0.0`, `v2.0.0-alpha.1` or `v1.7.0`
- **THEN** it is a valid release tag

#### Scenario: A non-conforming tag is rejected

- **WHEN** a tag reads `v1.7`, `V1.7.0` or `release-1.7.0`
- **THEN** it is rejected, because it is neither prefixed correctly nor a three-part version

### Requirement: Tag precedence order

The release tags SHALL form a sequence that increases under semver 2.0.0 precedence along the commit ancestry, so that a later version is never an ancestor of an earlier one.

#### Scenario: Milestones order correctly

- **WHEN** the tag line contains `v2.0.0-alpha.1`, `v2.0.0-beta.1` and `v2.0.0`
- **THEN** precedence reads `alpha.1 < beta.1 < 2.0.0`, and the ancestry order of their commits agrees

#### Scenario: An out-of-order tag is caught

- **WHEN** a proposed tag would place a numerically greater version on an earlier commit
- **THEN** the tag is rejected before it is created

### Requirement: Unreleased milestones carry a pre-release identifier

A milestone that was never published SHALL NOT be tagged as a normal version; it SHALL carry a pre-release identifier on the next version it belongs to.

#### Scenario: A never-published milestone is a pre-release

- **WHEN** a historical milestone predates the project's first public release and introduced a breaking change
- **THEN** its tag is a pre-release of the version that first release belongs to, not a normal version of its own

#### Scenario: A published version is a normal version

- **WHEN** a version has artifacts published for it
- **THEN** its tag carries no pre-release identifier and is never re-pointed to a different commit

### Requirement: Breaking change classification

A version increment SHALL follow semver 2.0.0 §6-§8 applied to the declared public API: an incompatible change to a declared surface MUST raise MAJOR, a backward-compatible addition MUST raise MINOR, and a backward-compatible fix MUST raise PATCH.

#### Scenario: Internal and cosmetic changes are not breaking

- **WHEN** a change touches internal C++ interfaces, the test tooling, error-message wording, or adds output files the previous version neither wrote nor promised
- **THEN** it is not a breaking change and does not raise MAJOR

#### Scenario: A declared surface change is breaking

- **WHEN** a change removes or renames a flag, changes a flag's accepted values, changes an exit code, changes a `config.json` key or its type, or moves the default job-state path
- **THEN** it is a breaking change and the next version raises MAJOR
