# progress-indeterminate-bar Specification

## Purpose

Covers the progress-bar system's indeterminate spinner mode: visual feedback for phases whose duration is known but whose progress cannot be measured (model loading, warmup), rendered as one of the context's bars and governed by the same lifecycle rules as determinate bars.

## Requirements

### Requirement: Indeterminate spinner bars

The progress-bar system SHALL support marking a bar indeterminate. An indeterminate bar SHALL render an animation that visibly advances on the context's repaint clock without any progress value being reported, SHALL NOT render an ETA badge, and SHALL animate only while its context is renderable (TTY stdout, not quiet, not cleared). The indeterminate mode SHALL NOT change the bar lifecycle: the context's clear/erase semantics apply unchanged, and a determinate bar on the same context SHALL keep its existing behavior.

#### Scenario: Spinner animates without progress reports

- **WHEN** a bar is marked indeterminate and its context repaints without any progress value having been set for that bar
- **THEN** the bar's rendered frame changes between repaints
- **AND** no ETA badge is rendered on that bar

#### Scenario: Spinner is cleared with its context

- **WHEN** a context holding an indeterminate bar is cleared
- **THEN** the spinner's line is erased with the context's other bars and is never drawn again

#### Scenario: Non-TTY output shows no spinner

- **WHEN** stdout is not a terminal and a bar is marked indeterminate
- **THEN** no spinner frames are written to stdout

#### Scenario: Determinate bars are unaffected

- **WHEN** a context holds both an indeterminate bar and a determinate bar with progress updates
- **THEN** the determinate bar renders its progress and ETA badge as before the indeterminate mode existed
