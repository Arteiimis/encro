# Spec Delta

## ADDED Requirements

### Requirement: The task follows the configured build mode and platform

The test task SHALL locate each suite's binary and its report paths through the loaded project configuration, so a run uses the build mode, the platform and the project build directory the caller selected instead of a fixed one.

#### Scenario: Non-release configuration runs the matching binaries

- **WHEN** the project is configured for a build mode other than `release` and the test task runs
- **THEN** the task builds the selected suites' targets in that configured mode
- **AND** it runs the binaries produced by that build rather than failing to find a `release` binary

#### Scenario: Documented report paths follow the configuration

- **WHEN** the test task runs under a configuration whose project build directory differs from the default one
- **THEN** the documented report and console paths are resolved inside that build directory
- **AND** a post-run read of those documented paths finds this run's output

#### Scenario: The self-check reports the configuration it resolved

- **WHEN** the task's self-check mode runs
- **THEN** it reports the resolved build mode, platform and suite binary path
- **AND** it fails when the configuration or that path cannot be resolved
