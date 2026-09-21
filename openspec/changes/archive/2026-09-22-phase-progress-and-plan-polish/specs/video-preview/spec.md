## ADDED Requirements

### Requirement: Preview summary line states the run's elapsed time

The written-to line of a preview run SHALL state the wall time the preview itself spent working, measured from the start of the run to the moment its progress bar is cleared (`progress-bar-lifecycle`) and rendered in the duration form defined by `pipeline-narration`. The line keeps its existing position and wording — it prints after the render completes, once per run, and the window list keeps printing ahead of it.

#### Scenario: Single-input preview reports its duration

- **WHEN** a single-input preview finishes rendering after 35 seconds
- **THEN** its written-to line reads `Preview written to: <output path> in 35s`

#### Scenario: Two-input preview reports its duration

- **WHEN** a two-input preview finishes rendering after 1 minute and 4 seconds
- **THEN** its written-to line ends with `in 1m:04s`, and it still prints exactly once, after the window list

#### Scenario: Bar is gone when the line prints

- **WHEN** either preview mode prints its written-to line on a terminal
- **THEN** the preview's progress bar has already been cleared from the screen
