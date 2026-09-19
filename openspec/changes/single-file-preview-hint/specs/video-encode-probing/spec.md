## MODIFIED Requirements

### Requirement: Encode summary hints at preview

After a successful encode (with probing or without), the final summary SHALL print a one-line hint showing the preview command for the encoded files, but only when its encode results carry exactly one successful video. When the results carry two or more successful videos the summary SHALL print no preview hint at all — no per-file hint lines and no truncation marker naming the omitted ones. `--dry-run` SHALL NOT print this hint.

#### Scenario: Summary carries the hint

- **WHEN** an encode run completes with exactly one successful video
- **THEN** the summary includes a hint line of the form `encro preview <original> <encoded>`

#### Scenario: Multi-file runs omit the hint

- **WHEN** an encode run completes with two or more successful videos
- **THEN** the summary prints the count line and no preview hint lines and no notice that hints were omitted

#### Scenario: Single success within a failing batch still carries the hint

- **WHEN** an encode run completes with exactly one successful video and one or more failed videos
- **THEN** the summary prints the failed-file list and the single preview hint for the successful video

#### Scenario: A recovered single task counts as the one success

- **WHEN** a resumed run recovers exactly one already-completed task from saved job state and encodes nothing new
- **THEN** the summary carries that task's preview hint

#### Scenario: Dry-run has no hint

- **WHEN** the user runs `--dry-run`
- **THEN** no preview hint is printed
