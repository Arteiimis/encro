# portable-fake-tool Delta

## ADDED Requirements

### Requirement: Invocations can be held at a gate file until released

The fake ffmpeg SHALL support invocation gating for mid-flight synchronization: when a gate file is configured, a gated invocation appends its invocation-log entry first and then blocks until the gate file exists, so the log entry proves the invocation started while it is being held. A fail-safe deadline SHALL end the block so a miswired test cannot hang the suite. A call-index variable SHALL designate the first gated invocation N (using the same invocation counter as the per-call schedule mechanism): invocations with index >= N are gated, invocations with index < N proceed normally; when the index variable is unset, every invocation is gated. Gating SHALL NOT apply to the ffprobe role, and `-version` probes SHALL neither be gated nor consume an invocation index.

#### Scenario: Later invocation held while earlier ones complete

- **WHEN** the fake tool is configured to gate from call 2 with a gate file that does not exist yet, and two invocations occur
- **THEN** the first invocation completes normally and its side effects are visible
- **AND** the second invocation's log entry is present while the invocation is still blocked
- **AND** creating the gate file releases the second invocation

#### Scenario: Index unset gates every invocation

- **WHEN** only the gate file variable is configured and a gate file does not exist
- **THEN** every ffmpeg invocation blocks until the gate file exists

#### Scenario: Gate never released

- **WHEN** a gated invocation is never released by its test
- **THEN** the invocation exits at the fail-safe deadline instead of hanging forever

#### Scenario: Version probes bypass gating

- **WHEN** the fake tool is invoked with `-version` while gating is configured
- **THEN** the version output is returned without blocking and without affecting invocation indexing
