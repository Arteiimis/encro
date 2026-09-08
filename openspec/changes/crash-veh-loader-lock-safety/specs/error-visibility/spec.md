## ADDED Requirements

### Requirement: DLL-initialization exceptions pass through crash interception

Notwithstanding the general fatal-exception first-chance reporting and
durable crash-record requirements: when a fatal-code exception is raised on a
thread that is inside a DLL-load zone (executing within a dynamic-library load
window the application itself initiated), the first-chance crash interception
SHALL NOT write a crash record and SHALL NOT capture a stacktrace for that
exception; normal exception dispatch SHALL continue so the library's own
exception handlers run. Any crash record written by another report path while
the reporting thread is inside a DLL-load zone SHALL omit the stacktrace
(reason and context only, with a marker noting the omission) rather than risk
blocking the process.

#### Scenario: Handled probe exception during a DLL load

- **WHEN** a loaded library's initialization raises a fatal-code exception
  that the library's own handler catches
- **THEN** no crash record is written and no stacktrace is captured
- **AND** execution continues and the process terminates normally (no hang)

#### Scenario: Unhandled exception on a DLL-load-zone thread

- **WHEN** an exception raised while the thread is inside a DLL-load zone is
  not handled and reaches the process's unhandled-exception or terminate path
- **THEN** the crash record is written with reason and context
- **AND** the record omits the stacktrace, carrying a marker that names the
  omission

#### Scenario: Crashes outside DLL-load zones are unchanged

- **WHEN** a fatal exception occurs on a thread outside any DLL-load zone
- **THEN** the crash record carries reason, context, and the symbolized
  stacktrace exactly as before this carve-out

#### Scenario: The zone is thread-scoped and temporary

- **WHEN** a thread has left its DLL-load zone, or another thread crashes
  while one thread is inside a zone
- **THEN** first-chance interception and stacktrace capture behave as before
  on the threads outside the zone
