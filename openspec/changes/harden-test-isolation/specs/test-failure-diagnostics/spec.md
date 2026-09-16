## ADDED Requirements

### Requirement: Crash diagnostics survive a test that replaces them

A test that replaces the run's crash context provider SHALL restore the provider installed for the run before the case ends, including on failure paths, so later crashes keep producing a crash record that names the running test. Restoring SHALL be done with a scoped guard whose destructor restores the previous provider.

#### Scenario: A test installs its own context provider

- **WHEN** a test sets a crash-context provider for its own scenario and then clears it
- **THEN** the provider installed for the run is in place again when the case ends
- **AND** a crash in a later test case still produces a crash record naming that test
