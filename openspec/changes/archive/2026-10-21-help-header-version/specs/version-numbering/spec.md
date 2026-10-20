# Spec Delta

## MODIFIED Requirements

### Requirement: Version string source of truth

The version printed by `encro --version` SHALL be generated at build time from the project's declared version, not written as a literal in source, and SHALL be printed in the tag's `v`-prefixed form.

#### Scenario: The binary reports the declared version

- **WHEN** the project declares version `2.0.0` and the binary is built
- **THEN** the version `encro --version` reports is `v2.0.0`, followed only by build metadata when the build carries a commit, and no other version string appears in its output

#### Scenario: A stale literal cannot survive

- **WHEN** the declared version changes and the binary is rebuilt
- **THEN** `encro --version` reports the new value without any source edit outside the declaration

#### Scenario: A build without git metadata still reports the version

- **WHEN** the tree carries no git metadata and the binary is built
- **THEN** `encro --version` reports `v2.0.0` with no commit suffix

#### Scenario: The version line carries no build time

- **WHEN** the user runs `encro --version`
- **THEN** the output contains no calendar date and no time of day

## ADDED Requirements

### Requirement: Build metadata names the commit

`encro --version` SHALL append the commit the binary was built from as semver build metadata, `+<short commit>`, whenever the build can read it from git. The suffix is metadata, not part of the version value: two builds sharing a version but differing in commit are the same version.

#### Scenario: The commit follows the version

- **WHEN** the source tree is at commit `a5f6f41c` and the binary is built
- **THEN** `encro --version` reports `v<declared version>+a5f6f41c`

#### Scenario: A rebuild picks up a new commit

- **WHEN** the working tree advances to a new commit and the binary is rebuilt without reconfiguring the project
- **THEN** `encro --version` reports the new short commit
