## MODIFIED Requirements

### Requirement: Output capture and stream merging

The child's standard output MUST be captured in full into the result. With stderr merging enabled (the default), standard error MUST be interleaved into the same captured output as standard out; with merging disabled, standard error MUST be captured in full into a separate field of the result (available to the caller, and never forwarded to the caller's own stderr).

#### Scenario: Merged stderr

- **WHEN** a command writes to both stdout and stderr with merging enabled
- **THEN** the result contains both streams' content

#### Scenario: Separate stderr

- **WHEN** a command writes to stderr with merging disabled
- **THEN** the result's merged output contains only stdout content, the child's stderr is available in the separate stderr field, and nothing is forwarded to the caller's own stderr

#### Scenario: Failed command exposes its stderr

- **WHEN** a command exits non-zero and has written diagnostic lines to stderr with merging disabled
- **THEN** the caller can read those lines from the result's stderr field after the command completes

#### Scenario: Large output

- **WHEN** a command produces output larger than a single pipe buffer
- **THEN** the full output is captured without deadlock and without truncation
