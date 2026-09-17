# test-isolation Specification

## Purpose

Keeps the test suite's verdict independent of execution order, reporter, stdio mode and shard count, by requiring every test case to restore the process-global state it mutates and every fixture to use paths that belong to the current run alone.

## Requirements

### Requirement: Test cases restore every process-global they mutate

A test case that mutates process-global state SHALL restore the previous state before it ends, including on failure paths. Restoring SHALL be done with a scoped guard whose destructor restores it, not with a statement at the end of the case body. Where a test destroys a global instead of mutating it — the process-wide default logger taken down by `logging::shutdown()` — restoring it immediately through a shared wrapper that re-installs the sink-less logger leaves nothing to restore at the end of the case and satisfies this requirement. The globals covered by this rule include, but are not limited to: the process-wide default logger and log level, the terminal colour and quiet modes, the stop-signal watchdog and armed deadline, and any seam that redirects process-wide behaviour for the duration of a test.

#### Scenario: A test shuts logging down

- **WHEN** a test shuts the logging subsystem down to exercise teardown
- **THEN** no later test case observes fallback log output on a stream it captures
- **AND** the default logger installed for the run is in place again before the next case starts

#### Scenario: A test sets the terminal colour mode

- **WHEN** a test changes the terminal colour mode
- **THEN** the previous mode is restored when the case ends, including when an assertion fails mid-case
- **AND** a later test that asserts an absence of escape sequences still passes

#### Scenario: A test arms the force-exit watchdog

- **WHEN** a test arms the stop-signal force-exit watchdog with a short grace period
- **THEN** the armed deadline and grace period are disarmed when the case ends, including when an assertion fails mid-case
- **AND** the remaining suite is never terminated by that watchdog

### Requirement: Stdio capture is RAII and assertions stay outside the redirect window

Capturing a stdio stream in a test SHALL be done with a scoped helper that restores the original stream on every exit path, including a failing assertion or an exception, and that reports its own failures only after the redirect has ended. A test SHALL NOT execute an assertion while any stream is redirected, because the test reporter writes to those streams and those bytes would land in the captured file.

#### Scenario: An assertion fails inside a capture window

- **WHEN** a test asserts while its stdout is redirected to a capture file
- **THEN** the failure is reported as that test's failure only
- **AND** the process's stdout is back to its original destination for the following test cases

#### Scenario: A capture helper cannot redirect the stream

- **WHEN** a capture helper cannot open its capture file or duplicate the stream descriptor
- **THEN** the failure is reported after the redirect ends, with the stream left in its original state

#### Scenario: The reporter echoes successful assertions

- **WHEN** the suite runs with a reporter mode that reports successful assertions (for example `-r console -s`)
- **THEN** captured file contents contain only what the code under test wrote
- **AND** the verdict and the assertion count match a run without that reporter mode

### Requirement: Verdicts and assertion counts are invocation-independent

The suite's verdict and its reported assertion count SHALL be identical for the same build across execution orders, reporter modes, terminal attachment, and shard counts. Test bodies SHALL NOT depend on ambient properties of the invocation — execution order, reporter mode, terminal attachment, stdio mode, or shard count. Where the behaviour under test legitimately depends on the host, the test SHALL derive its expectation the same way the code under test does (for example, the same parallelism source), so the expectation stays correct on any machine. The shuffled execution order SHALL stay visible: the run's log SHALL carry the seed the order was derived from, so any order-dependent failure can be reproduced.

#### Scenario: Two runs with different orders

- **WHEN** the unit suite is run twice, each with the order reported in its own log
- **THEN** every test case passes in both runs
- **AND** the executed assertion locations and their counts are identical between the runs

#### Scenario: Order dependence surfaces in CI

- **WHEN** a test case depends on state another case left behind
- **THEN** the CI run fails and its log states the seed used for that run
- **AND** re-running locally with that seed reproduces the failure

#### Scenario: A host-dependent expectation

- **WHEN** a test asserts on a value the code under test derives from host properties such as available parallelism
- **THEN** the expected value is derived from the same source rather than hardcoded, and the case passes on machines with different core counts

### Requirement: Fixtures use per-run private paths

Test fixtures SHALL confine their writes to a directory private to the current test case and process. Test temporary directory names SHALL be unique per process, not derived from the clock alone. No test SHALL write to, or delete, a shared application directory, and no test SHALL reference a machine-specific user path or watch a path it shares with other tests.

#### Scenario: Shards run concurrently

- **WHEN** several test shards run at the same time on one machine
- **THEN** no test case observes a path created or removed by another shard

#### Scenario: A test needs a scratch directory

- **WHEN** a test exercises code that creates its own scratch directory
- **THEN** the test points that code at its own temporary directory
- **AND** the shared application scratch directory is left untouched

#### Scenario: A test watches the working directory

- **WHEN** a test asserts that a file did not appear in a directory the code under test can write to
- **THEN** the watched directory belongs to that test case
- **AND** the assertion is evaluated by the case itself rather than from a destructor

#### Scenario: A fixture needs an absolute path

- **WHEN** a test needs a path outside its temporary directory
- **THEN** the path is derived at run time (environment, source root definition, or temporary root) rather than hardcoded to one machine
