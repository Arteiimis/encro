## MODIFIED Requirements

### Requirement: CI publishes the merged coverage report

The CI pipeline SHALL run the coverage job with e2e included and SHALL upload the HTML report as a workflow artifact so the merged, filtered report is inspectable per run. The HTML report and the merged profile data SHALL also be included inside the unified per-run artifact (`ci-run-*`) via the coverage report artifact.

#### Scenario: CI coverage artifact reflects merged scope

- **WHEN** the CI coverage job completes on a push
- **THEN** a coverage report artifact is uploaded containing the HTML report generated from the unit+e2e merged profile with the exclusions applied

#### Scenario: Coverage report lands in the unified artifact

- **WHEN** the CI coverage job completes as part of a workflow run
- **THEN** the unified `ci-run-*` artifact contains a `coverage-report` subdirectory holding the HTML report and the merged profile data