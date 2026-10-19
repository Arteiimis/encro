## ADDED Requirements

### Requirement: Command availability follows the inference backend

`encro organize` SHALL run only where this build can classify images, which is the case when the build links the local ONNX engines or when the fake-tagger seam (`ENCRO_FAKE_TAGGER`) is set — the fake engine counts as a backend. Where neither holds the command SHALL be unavailable, and the rest of this capability describes only its available runs; how an unavailable build presents the command in its help and completion surfaces is governed by `cli-help-layout` and `shell-completion`.

#### Scenario: A build with the local engines is available

- **WHEN** a build that links the ONNX engines runs `encro organize <directory>`
- **THEN** the command runs as the rest of this capability specifies

#### Scenario: The fake backend makes the command available on any platform

- **WHEN** a build without the ONNX engines runs `encro organize <directory>` with `ENCRO_FAKE_TAGGER` pointing at a fixture
- **THEN** the command runs and files images according to that fixture, exactly as it would on a build with the local engines

#### Scenario: A build with no backend has an unavailable command

- **WHEN** a build without the ONNX engines runs `encro organize <directory>` with `ENCRO_FAKE_TAGGER` unset
- **THEN** the command is unavailable and the refusal requirement below applies

### Requirement: Unavailable invocation is refused before any work

An `encro organize` invocation that passes argument validation on a build where the command is unavailable SHALL exit non-zero with an error naming the missing inference backend, before scanning, before creating or copying any file, and before any model download. The refusal SHALL NOT replace argument validation, and `encro organize -h` SHALL still print the subcommand help.

#### Scenario: An unavailable run touches nothing

- **WHEN** a build with no backend runs `encro organize <directory>` over a directory holding images
- **THEN** the command exits non-zero with an error naming the missing inference backend
- **AND** no `organized/` tree and no other file is created or modified

#### Scenario: An unavailable run does not download models

- **WHEN** a build with no backend runs `encro organize --download-models`, with or without a directory
- **THEN** the command exits non-zero with the same error and no download is attempted

#### Scenario: Argument errors still come first

- **WHEN** a build with no backend runs `encro organize` with neither a directory nor `--download-models`
- **THEN** the missing-directory argument error and the help hint are printed, not the backend error

#### Scenario: Help still works when the command is unavailable

- **WHEN** a build with no backend runs `encro organize -h`
- **THEN** the organize subcommand help is printed and the exit code is 0
