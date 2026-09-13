# progress-scroll-label Specification

## Purpose

Provides a scrolling (marquee-style) display for progress-bar postfix text that overflows the terminal's postfix width, so long encode filenames and status text remain readable on narrow terminals instead of being cut off with an ellipsis.

## Requirements

### Requirement: Overflowing postfix text scrolls instead of truncating

Progress-bar postfix text (e.g., `Encoding: <filename> | <status>`) whose display width exceeds the available postfix budget SHALL be shown as a continuously scrolling window of the full text instead of being truncated with an ellipsis. Text that fits within the budget SHALL be shown statically in full. The scrolling window SHALL be no wider than the postfix budget, SHALL advance monotonically over time, SHALL wrap around to the beginning of the text, and SHALL never cut a UTF-8 code point in half.

#### Scenario: Long filename on narrow terminal scrolls

- **WHEN** the encoded filename makes the postfix text wider than the postfix budget
- **THEN** a window of the full text no wider than the budget is shown
- **AND** the window advances over time to the end of the text, pauses briefly, then reverses direction and scrolls back, alternating continuously
- **AND** all text, including the full filename, is eventually visible while the task runs
- **AND** the window never shows a blank region: there is no scroll phase where the window contains only padding instead of text

#### Scenario: Text fits within budget stays static

- **WHEN** the postfix text is no wider than the postfix budget
- **THEN** the full text is shown without scrolling or ellipsis truncation

#### Scenario: Ellipsis truncation no longer used for postfix overflow

- **WHEN** postfix text overflows the budget
- **THEN** no ellipsis is appended and the scroll window replaces the old truncated-with-ellipsis display

#### Scenario: UTF-8 names scroll without corruption

- **WHEN** the postfix text contains multi-byte UTF-8 characters (e.g., CJK filenames)
- **THEN** every rendered frame of the scroll window contains only whole code points

#### Scenario: Only the label part scrolls, status stays fixed

- **WHEN** the postfix text has multiple parts separated by ` | ` (e.g., `Encoding: <filename> | segment 1/1`)
- **THEN** only the first part (the label, e.g., `Encoding: <filename>`) scrolls within its own window
- **AND** the remaining parts (e.g., the segment status) and the ` | ` delimiters stay in a fixed position and never scroll off-screen
- **AND** the full display width never exceeds the postfix budget

#### Scenario: Oversized status is truncated, not scrolled

- **WHEN** the fixed status part alone is too wide to leave a minimal scrolling window for the label
- **THEN** the status part is truncated with an ellipsis and the label keeps a minimal scrolling window

### Requirement: ETA prefix stays visible while content scrolls

When a progress bar shows an ETA prefix, the ETA prefix SHALL remain visible (not scrolled away) while the rest of the postfix text scrolls. The scroll window applies only to the postfix content, never to the ETA prefix.

#### Scenario: ETA shown while encode label scrolls

- **WHEN** a bar has an ETA prefix and the postfix text overflows the budget
- **THEN** the ETA prefix is always visible on every redraw
- **AND** only the postfix content (e.g., `Encoding: <filename> | <status>`) scrolls within the remaining width

### Requirement: Encode label and status reach the scroller untruncated

The filename label and status text passed to the progress postfix SHALL NOT be pre-truncated at the current fixed limits (48-column filename label, 72-column status line) before reaching the scroll display. A sanity cap on raw ffmpeg status lines SHALL remain so a single pathological line cannot consume unbounded memory; the cap SHALL be large enough that realistic filenames and status text are unaffected.

#### Scenario: Filename longer than 48 columns scrolls fully

- **WHEN** an encoded file's name is longer than 48 display columns
- **THEN** the full name appears in the postfix text and is reachable via the scroll window

#### Scenario: Status line longer than 72 columns scrolls fully

- **WHEN** a reported encode status line is longer than 72 characters but under the sanity cap
- **THEN** the full status text appears in the postfix text and is reachable via the scroll window

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
