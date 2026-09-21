# progress-bar-lifecycle Specification

## Purpose


Defines the lifetime of a phase's progress bars: they exist only while the phase that owns them has work in flight, and the phase hands the screen back to plain output the moment its work ends.

## Requirements

### Requirement: Progress bars are scoped to the phase that renders them

Every phase that renders progress bars SHALL clear all of its bars the moment its work ends — on success, on cancellation, and on failure — and SHALL do so before it prints its own summary line or product output. Clearing SHALL stop the periodic repaint of those bars, so no finished bar line survives into the next phase or into the run summary.

#### Scenario: Encode phase ends

- **WHEN** an encode batch finishes with its overall and per-worker bars on screen
- **THEN** every bar line of that batch is removed before the encode summary line prints

#### Scenario: Probe phase ends

- **WHEN** the probing phase finishes
- **THEN** the probe's bars are removed before the plan block prints

#### Scenario: Canceled phase clears its bars

- **WHEN** a phase's work is interrupted by a stop request after its bars rendered
- **THEN** its bars are removed before any later output, and no bar line survives into a later phase

#### Scenario: Failing phase clears its bars

- **WHEN** a phase fails after its bars rendered
- **THEN** its bars are removed before the failure diagnostic or any other later output prints

### Requirement: Cleared bars are never drawn again

After a phase's bars have been cleared, that phase's bar rendering SHALL NOT touch the screen again: no bar line is drawn, and no cursor movement or line erase targets output already written above the cleared block. A phase that wants bars after a clear starts a fresh set of bars.

#### Scenario: Update arriving after the clear paints nothing

- **WHEN** a progress update from a finished phase arrives after its bars were cleared
- **THEN** no bar line is drawn and no cursor movement or line erase reaches output above the cleared block

#### Scenario: Next phase renders in place

- **WHEN** the next phase renders its bars after the previous phase's bars were cleared
- **THEN** the new bars render at the cursor position the clear left behind, and only their own lines are repainted
