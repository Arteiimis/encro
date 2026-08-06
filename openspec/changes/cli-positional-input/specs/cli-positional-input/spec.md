## Purpose

Allows passing input paths as positional arguments instead of requiring `-i/--input` or `-I/--inputs`, matching common CLI conventions.

## ADDED Requirements

### Requirement: Single positional input behaves like -i

A single positional argument SHALL be treated as the input path, with the same semantics as `-i/--input`: it may be a file or a directory, and it works in video mode, picture mode, and pack-only mode.

#### Scenario: Single directory positional in video mode
- **WHEN** the user runs `encro <dir>` without `-i` or `-I`
- **THEN** the directory is scanned for videos and encoded, same as `encro -i <dir>`

#### Scenario: Single file positional in video mode
- **WHEN** the user runs `encro <file>`
- **THEN** the file is encoded, same as `encro -i <file>`

#### Scenario: Single directory positional with pack-only
- **WHEN** the user runs `encro <dir> -z`
- **THEN** the directory is packed, same as `encro -z -i <dir>`

#### Scenario: Single directory positional in picture mode
- **WHEN** the user runs `encro <dir> -t picture`
- **THEN** the picture workflow runs on the directory, same as `encro -t picture -i <dir>`

### Requirement: Multiple positional inputs behave like -I

Two or more positional arguments SHALL be treated as multiple input paths with the same semantics as `-I/--inputs`: video mode only, regular files only, and incompatible with `-z/--pack-only`.

#### Scenario: Multiple file positionals encode like -I
- **WHEN** the user runs `encro <a.mp4> <b.mp4>`
- **THEN** both files are encoded with the same behavior as `encro -I <a.mp4> <b.mp4>`

#### Scenario: Multiple positional inputs with picture mode fails
- **WHEN** the user runs `encro <a.mp4> <b.mp4> -t picture`
- **THEN** the run fails with an error stating that multiple inputs are only supported in video mode

#### Scenario: Multiple positional inputs with pack-only fails
- **WHEN** the user runs `encro <a.mp4> <b.mp4> -z`
- **THEN** the run fails with an error stating that pack-only requires a single input

#### Scenario: Multiple positional inputs containing a directory fails
- **WHEN** the user runs `encro <dir> <file.mp4>`
- **THEN** the run fails with an error explaining that multiple input paths must be regular files

### Requirement: Positional inputs conflict with -i and -I

Passing positional arguments together with `-i/--input` or `-I/--inputs` SHALL be rejected with an error naming the conflict.

#### Scenario: Positional mixed with -i fails
- **WHEN** the user runs `encro <a> -i <b>`
- **THEN** the run fails with an error explaining that positional inputs and `-i/--input` cannot be combined

#### Scenario: Positional mixed with -I fails
- **WHEN** the user runs `encro <a> -I <b> <c>`
- **THEN** the run fails with an error explaining that positional inputs and `-I/--inputs` cannot be combined

### Requirement: Positional input documented in help

The help output SHALL document the positional input form in the usage lines and list the positional option under Input/Output options.

#### Scenario: Usage shows positional form
- **WHEN** the user runs `encro -h` or `encro -hh`
- **THEN** the usage section shows the positional input form, e.g. `encro <input>...`, alongside the existing `-i`/`-I` forms

#### Scenario: Positional option listed in IO group
- **WHEN** the user runs `encro -hh`
- **THEN** the Input/Output options section lists the positional input option with a description stating it accepts one or more files or directories

### Requirement: Missing input error hints at positional form

When no input is provided at all, the error SHALL mention that a directory or file list can be passed directly.

#### Scenario: No input error message
- **WHEN** the user runs `encro` without any input and without `-i`/`-I`
- **THEN** the run fails with an error explaining that an input path is required and that it may be passed as a positional argument
