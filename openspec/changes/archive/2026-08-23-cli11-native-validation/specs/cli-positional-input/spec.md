# cli-positional-input Specification (Delta)

## Purpose

Allows passing input paths as positional arguments instead of requiring `-i/--input` or `-I/--inputs`, matching common CLI conventions.

## MODIFIED Requirements

### Requirement: Positional inputs conflict with -i and -I

Passing positional arguments together with `-i/--input` or `-I/--inputs` SHALL be rejected during argument parsing by the native exclusion check, with a native-style error message naming both options.

#### Scenario: Positional mixed with -i fails
- **WHEN** the user runs `encro <a> -i <b>`
- **THEN** the run fails during argument parsing with an error naming both the positional option and `-i/--input`

#### Scenario: Positional mixed with -I fails
- **WHEN** the user runs `encro <a> -I <b> <c>`
- **THEN** the run fails during argument parsing with an error naming both the positional option and `-I/--inputs`