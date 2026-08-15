## Purpose

Defines the behavior contract for the `xmake tidy` static-analysis command: a report-only clang-tidy scan scoped to project code, with a curated check set, text and SARIF output, and function-length/cognitive-complexity guardrails.

## ADDED Requirements

### Requirement: tidy scans project code with a curated check set

The `xmake tidy` command SHALL run clang-tidy over every translation unit listed in the project's compile database, using a curated check set that includes semantic-analyzer, bug-pattern, performance, and portability checks plus function-length and cognitive-complexity guardrails. The check set SHALL NOT include checks that contradict project conventions (notably the `#pragma once` header convention).

#### Scenario: Scan covers the whole project

- **WHEN** the user runs `xmake tidy` from the project root
- **THEN** every `.cpp` translation unit in `build/compile_commands.json` is scanned
- **AND** warnings are produced only by checks in the curated set

#### Scenario: Convention-conflicting check is absent

- **WHEN** a project header uses `#pragma once`
- **THEN** no "use include guards instead" style warning is reported for it

### Requirement: Warnings are scoped to project code

`xmake tidy` SHALL report warnings only from `src/` and `tests/`, and SHALL NOT report warnings originating in third-party dependency headers.

#### Scenario: Third-party headers are excluded

- **WHEN** a translation unit includes dependency headers such as Boost, fmt, or spdlog
- **THEN** no warning from those headers appears in the output

#### Scenario: Project headers are included

- **WHEN** a warning exists in a `src/` or `tests/` header
- **THEN** that warning appears in the output with its source location

### Requirement: Text output is the default

When invoked without flags, `xmake tidy` SHALL print each warning with its file, line, message, and check name, followed by a summary count, and SHALL exit 0.

#### Scenario: Default run lists warnings

- **WHEN** the user runs `xmake tidy` with no flags
- **THEN** each warning is printed with file, line, message, and check name
- **AND** a total warning count is printed at the end

### Requirement: SARIF output

`xmake tidy --sarif` SHALL write warnings in SARIF format so downstream tooling and agents can consume them machine-readably.

#### Scenario: SARIF file is produced

- **WHEN** the user runs `xmake tidy --sarif`
- **THEN** a SARIF file is written containing one result per warning, each with file, line, message, and rule id

### Requirement: Report-only semantics

`xmake tidy` SHALL exit 0 whether or not warnings are found. The presence of warnings SHALL NOT fail the command.

#### Scenario: Warnings do not fail the run

- **WHEN** the scan finds warnings
- **THEN** the command exits 0 after reporting them

### Requirement: Complexity guardrails

The curated check set SHALL flag functions whose length exceeds a threshold of 80 lines and functions whose cognitive complexity exceeds a threshold of 25.

#### Scenario: Over-long function is flagged

- **WHEN** a function exceeds 80 lines
- **THEN** a function-size warning is reported for that function

#### Scenario: Over-complex function is flagged

- **WHEN** a function's cognitive complexity exceeds 25
- **THEN** a cognitive-complexity warning is reported for that function

### Requirement: Missing compile database fails with a clear error

When the compile database is absent, `xmake tidy` SHALL exit non-zero with a message directing the user to build first.

#### Scenario: No compile database

- **WHEN** the user runs `xmake tidy` without `build/compile_commands.json` present
- **THEN** the command exits non-zero with a message pointing to the missing file
