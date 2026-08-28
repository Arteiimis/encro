# coverage-report Specification

## Purpose

Defines the behavior contract for the `xmake coverage` command and its CI integration: what the coverage report counts (unit and e2e suites merged into one profile), what it excludes from measurement, the output forms it produces, and the artifact CI publishes.

## Requirements

### Requirement: Coverage counts unit and e2e suites in one merged profile

The `xmake coverage` command SHALL support running both the unit suite and the e2e suite under instrumentation and merging both runs' profile data into a single report; `--e2e` SHALL include the e2e run. The e2e suite drives the real product binary, so its counters SHALL contribute to the same per-source line/function/branch totals as the unit suite.

#### Scenario: e2e inclusion raises entry-layer coverage

- **WHEN** the user runs `xmake coverage --e2e`
- **THEN** the per-file report includes counts contributed by the e2e run for sources executed only through the real binary entry path (app entry and startup modules)

#### Scenario: Default run excludes e2e

- **WHEN** the user runs `xmake coverage` without `--e2e`
- **THEN** only the unit suite runs under instrumentation and only its counters are reported

### Requirement: Report excludes non-instrumentable and non-project sources

The report SHALL exclude test sources and platform-bound infrastructure sources that cannot be meaningfully exercised in-process (console/terminal control, OS signal handling, crash handlers, OS file-open helpers) via the coverage filter. Product sources outside these categories SHALL remain in the report.

#### Scenario: Platform-bound sources are absent from the per-file table

- **WHEN** a coverage report is generated
- **THEN** the platform-bound infrastructure sources (terminal, stop-signal, crash-runtime, file-open) do not appear in the per-file table or totals
- **AND** all other `src/` sources remain present

#### Scenario: Test sources remain excluded

- **WHEN** a coverage report is generated
- **THEN** files under the test directories do not appear in the per-file table or totals

### Requirement: Report output forms

`xmake coverage` SHALL produce a per-file text report with region/function/line/branch totals; `--summary` SHALL limit console output to the totals row; `--html` SHALL additionally write an HTML report to `build/coverage/html` generated from the test binary only (it contains all project sources), without producing an empty report.

#### Scenario: Summary mode prints totals only

- **WHEN** the user runs `xmake coverage --summary`
- **THEN** the console shows the aggregate totals row and not a per-file listing

#### Scenario: HTML report is generated and populated

- **WHEN** the user runs `xmake coverage --html`
- **THEN** `build/coverage/html` contains a populated HTML report covering the project's sources

### Requirement: CI publishes the merged coverage report

The CI pipeline SHALL run the coverage job with e2e included and SHALL upload the HTML report as a workflow artifact so the merged, filtered report is inspectable per run.

#### Scenario: CI coverage artifact reflects merged scope

- **WHEN** the CI coverage job completes on a push
- **THEN** a coverage report artifact is uploaded containing the HTML report generated from the unit+e2e merged profile with the exclusions applied