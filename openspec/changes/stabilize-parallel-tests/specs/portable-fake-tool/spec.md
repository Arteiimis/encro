# portable-fake-tool Delta

## ADDED Requirements

### Requirement: Invocations can be gated from a configurable call index

The fake ffmpeg SHALL support an environment variable designating a call index N (counting invocations of the current schedule) such that invocations with index >= N append their invocation-log entry and then block until the gate file exists, while invocations with index < N proceed normally without gating. The gate setting MUST combine with the existing call-count and call-plan mechanisms, and blocking SHALL apply after the invocation is logged so a log entry proves the gated invocation started. A fail-safe deadline SHALL end the block so a miswired test cannot hang the suite. When the from-call variable is unset, the existing behavior (all invocations gate when a gate file is configured) is unchanged.

#### Scenario: Later invocation held while earlier ones complete

- **WHEN** the fake tool is configured to gate from call 2 with a gate file that does not exist yet, and two invocations occur
- **THEN** the first invocation completes normally and its side effects are visible
- **AND** the second invocation's log entry is present while the invocation is still blocked
- **AND** creating the gate file releases the second invocation

#### Scenario: Gate never released

- **WHEN** a gated invocation is never released by its test
- **THEN** the invocation exits at the fail-safe deadline instead of hanging forever

#### Scenario: Unset from-call keeps whole-run gating

- **WHEN** only the gate file variable is configured
- **THEN** every invocation gates, exactly as before this extension
