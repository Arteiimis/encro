## ADDED Requirements

### Requirement: Every progress postfix uses the ` | ` label/status grammar

Progress-bar postfixes SHALL separate a scrolling label from pinned status parts with ` | ` in every mode (encode, probe, preview, pack); which part scrolls and which stay fixed is defined by the "Overflowing postfix text scrolls instead of truncating" requirement. Separators the scroller does not recognize (e.g. `·`) SHALL NOT be used to join label and status, because such text counts as one part and its status then scrolls out of view on narrow terminals. A postfix with no ` | ` separator (idle, completion, and failure labels) SHALL scroll as one window.

#### Scenario: Probe slot bar keeps its CQ/sub-step status pinned

- **WHEN** a probe slot bar's postfix is `Probing: <filename> | CQ 20 scoring` and the filename makes the postfix wider than the postfix budget
- **THEN** only the `Probing: <filename>` part scrolls within its window
- **AND** every rendered frame still shows the `| CQ 20 scoring` status at a fixed position, including the sub-step changes (`encoding`, `scoring`, `scored`)

#### Scenario: Preview bar keeps its scored-window status pinned

- **WHEN** a preview bar's postfix overflows the postfix budget and carries a status part
- **THEN** the label part scrolls and the status part stays visible at a fixed position

#### Scenario: Single-part label scrolls as a whole

- **WHEN** a bar's postfix has no ` | ` separator (e.g. `Done: <long filename>`) and overflows the budget
- **THEN** the whole text scrolls in one window

### Requirement: Scrolling repaints on its own timer in every mode

Every progress bar whose postfix overflows the budget SHALL keep advancing its scroll window on a repaint cadence of its own, independent of progress-data updates: the window SHALL keep moving while the underlying task reports no new progress (a long probe step, a window score, a long single-file pack step, an unchanged ffmpeg progress file), and encode, probe, preview, and pack bars SHALL animate by the same rule. Repaints SHALL be display-only: a repaint SHALL NOT change a bar's progress value and SHALL NOT re-seed its remaining-time estimate. Repaint activity SHALL exist only while the context holds bars, and SHALL emit nothing when stdout is not a terminal.

#### Scenario: Probe bar keeps scrolling between steps

- **WHEN** a probe slot bar's label overflows the budget and its current probe step runs for 30 seconds without any progress update
- **THEN** the scroll window keeps advancing throughout that interval instead of freezing until the next update

#### Scenario: All modes scroll at the same rate

- **WHEN** encode, probe, preview, and pack bars all show an overflowing label and no progress update arrives for at least one second
- **THEN** each bar's window advances by the same time-derived offset rule (same columns per second, same pause at the text ends)

#### Scenario: Repaint does not disturb progress or ETA

- **WHEN** repaints occur while no progress update arrives
- **THEN** the bar's progress value stays at the last reported value
- **AND** the remaining-time estimate stays at the value derived from the last progress sample, while the elapsed part keeps counting from the task start

#### Scenario: No repaint activity without bars or without a terminal

- **WHEN** a progress context holds no bars, or stdout is not a terminal
- **THEN** no frame is emitted and no repaint work is done beyond the repaint deadline itself
