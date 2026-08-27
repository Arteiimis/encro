# portable-fake-tool Specification

## Purpose

Defines the contract for the cross-platform fake ffmpeg/ffprobe used by the unit test suite: how it is selected and driven, and the guarantee that process-spawning unit tests execute on every supported platform instead of being compiled away by platform guards.

## Requirements

### Requirement: Process-spawning unit tests compile and run on all supported platforms

The unit test suite SHALL NOT exclude any test case from compilation on any supported platform solely because it impersonates an external media tool. Orchestration flows that spawn ffmpeg/ffprobe-like processes (picture compression, preview generation, app pipelines, video processing) SHALL be exercised through the shared fake tool on both Windows and POSIX platforms.

#### Scenario: Unit suite runs on Linux CI

- **WHEN** the unit test suite is built and executed on Linux
- **THEN** zero test cases are excluded from compilation solely because they impersonate an external media tool
- **AND** the picture, preview, app-pipeline, and video-process orchestration suites all execute their process-spawning cases

#### Scenario: CI coverage reflects orchestration layers

- **WHEN** the CI coverage job runs the unit suite on Linux
- **THEN** every subprocess orchestration source file reports double-digit line coverage

### Requirement: Fake tool role is selected by executable basename

A copy of the fake tool whose executable basename is `ffprobe` SHALL respond as ffprobe; any other basename SHALL respond as ffmpeg. This role detection MUST work with either the platform-native executable suffix or no suffix.

#### Scenario: Role copies share one binary

- **WHEN** two copies of the fake tool binary named `ffmpeg` and `ffprobe` are spawned
- **THEN** the `ffprobe` copy emits probe-format JSON output controls and the `ffmpeg` copy performs encode-side behaviors

### Requirement: Fake tool behaviors are controlled via environment variables at invocation time

Fake tool behavior knobs (exit code, whether/how an output file is created including missing parent directories, partial-output-before-failure, stderr text, progress-frame emission, probe JSON content, invocation logging) SHALL be configurable exclusively through environment variables read at each invocation. Behavior extensions needed by ported tests (such as per-invocation scheduling of delay and exit code, or suppressing optional progress fields) SHALL use the same environment-variable mechanism rather than per-platform scripting.

#### Scenario: Failure requested via environment variable

- **WHEN** the fake ffmpeg is spawned with its failure knob enabled (non-zero exit code)
- **THEN** the child process exits with the requested non-zero code
- **AND** any output-file side effects requested for the failing variant still occur before exit

#### Scenario: Scheduled call outcomes

- **WHEN** the fake tool is configured with a per-invocation schedule (for example one delayed invocation followed by failures beyond a set count)
- **THEN** each invocation follows its scheduled in-flight delay and exit code
- **AND** the parent's retry handling observes both outcomes within one test process

#### Scenario: Invocation arguments are inspectable

- **WHEN** a test needs to assert the exact command line passed to the fake tool
- **THEN** the fake tool records each invocation's arguments to the invocation-log location and the test reads them back from disk

### Requirement: Test-managed environment variables do not leak between test cases

Helpers that set environment variables for a test case SHALL restore the previous environment state when the test case ends, so later test cases and spawned processes observe a clean environment regardless of execution order.

#### Scenario: Case ends with modified variables

- **WHEN** a test case sets or overrides fake-tool variables through the scoped helper and finishes (pass or fail)
- **THEN** the affected variable names hold their pre-case values afterwards, and unset names remain unset

### Requirement: Spawning layer launches fakes without a shell wrapper

The unit suite's process-spawning tests SHALL launch the fake tool by direct executable path on every supported platform; no platform-specific shell wrapper (such as `cmd.exe /c`) may be required to make the fake runnable.

#### Scenario: Direct spawn on POSIX

- **WHEN** a test configures the toolchain with a copied fake-tool path and the orchestration layer spawns it on Linux
- **THEN** the child runs without any shell-invocation layer and its behavior knobs take effect
