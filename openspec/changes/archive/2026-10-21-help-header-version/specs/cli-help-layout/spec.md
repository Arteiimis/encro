# Spec Delta

## ADDED Requirements

### Requirement: Main-help header names the release

The main help (`-h` and `-hh`) SHALL open with one header line naming the tool, its one-line purpose, and the release the binary was built from, joined by ` | ` in the form `encro: <purpose> | <release token>`. The release token SHALL be the token `encro --version` reports. The header SHALL NOT carry a build date or time of day.

#### Scenario: A build with git metadata shows version and commit

- **WHEN** the binary was built with git metadata and the user runs `encro -h`
- **THEN** the first line reads `encro: Universal video encoder/converter/packer | v<declared version>+<short commit>`

#### Scenario: A build without git metadata shows the version alone

- **WHEN** the binary was built without git metadata and the user runs `encro -hh`
- **THEN** the first line ends with ` | v<declared version>` and carries no `+` suffix

#### Scenario: No build timestamp in the header

- **WHEN** the user runs `encro -h` or `encro -hh`
- **THEN** the first line contains no calendar date and no time of day
