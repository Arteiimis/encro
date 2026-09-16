# pack-progress-status Specification

## Purpose

Defines what the compact (default) packing progress line shows while archives are packed and finalized: one composed status that keeps the packing label in place, a finalizing indicator that steps its frame on a fixed 120 ms cadence, and the destinations that must exist before the indicator runs at all.

## Requirements

### Requirement: Packing label stays the head of the compact packing postfix

While a compact packing run has work in flight, the packing label `Packing: archive <k>/<N> [file <n>/<m>]` SHALL remain the head of the progress postfix and SHALL NOT be replaced or suppressed by the finalizing indicator. While one or more archives are being finalized (their last entry added, archive trailer not yet written), the indicator SHALL be appended to that label as a separate ` | ` part, and no status text published during a finalizing window SHALL show the indicator without the label. The existing postfix layout rules may truncate the appended part on a terminal too narrow for it; the label stays the postfix head and keeps its counters.

#### Scenario: An archive finalizes while other archives still pack

- **WHEN** one of several in-flight archives enters finalization while the remaining archives still pack entries
- **THEN** the status texts published from that point until the window closes are the packing label followed by a ` | Finalizing <frame>` part
- **AND** the label carries the current archive and file counters, including progress updates that arrive while the window is open

#### Scenario: Last archive finalizes after the last entry is packed

- **WHEN** the final archive's trailer is written after every entry has been packed
- **THEN** the status text is the packing label followed by the ` | Finalizing <frame>` part
- **AND** the label's archive counter shows the archives completed so far, so it reads `archive <N-1>/<N>` until that close completes

#### Scenario: Completion replaces the finalizing indicator

- **WHEN** every archive has been finalized
- **THEN** the status text is the completion text `Packed: archive <N>/<N> complete`, with no finalizing indicator part

### Requirement: Finalizing frame steps on a fixed 120 ms cadence

The finalizing indicator SHALL step through its four-frame cycle no faster than once per 120 ms: over a window of `T` ms the shown frame SHALL change at most `T/120 + 1` times, however many status texts are published in that window. Publishing several status texts inside one 120 ms interval SHALL NOT advance the frame more than once in that interval.

#### Scenario: Frame steps while nothing else changes

- **WHEN** an archive is being finalized for 500 ms and no packing progress update arrives in that window
- **THEN** the frame changes at most 5 times
- **AND** at least two distinct frames of the four-frame cycle are shown

#### Scenario: Concurrent packing updates do not accelerate the frame

- **WHEN** another archive keeps publishing packing progress updates while a finalizing window is held open
- **THEN** the frame still changes at most `T/120 + 1` times in that window

#### Scenario: No continuous repaint loop

- **WHEN** a finalizing window is held open with no progress updates arriving
- **THEN** the frame advances about once per 120 ms instead of on every published status text

### Requirement: Finalizing indicator runs only while a destination can receive its output

The finalizing indicator SHALL run only while at least one destination can receive its output: renderable progress bars (a terminal on stdout with bars enabled), or an installed status-text consumer. While neither destination exists, a compact packing run SHALL publish no finalizing indicator text, and finalization itself SHALL be unaffected.

#### Scenario: Status consumer alone keeps the indicator alive

- **WHEN** progress bars are not renderable but a status-text consumer is installed
- **THEN** the indicator still publishes its composed status texts to that consumer

#### Scenario: No destination means no indicator output

- **WHEN** a compact packing run executes with bars not renderable and no status-text consumer installed
- **THEN** the run completes successfully
- **AND** no finalizing indicator text is produced
