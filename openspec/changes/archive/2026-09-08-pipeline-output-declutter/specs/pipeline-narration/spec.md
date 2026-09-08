## Purpose

Defines which narration lines each processing phase prints to the console and their wording conventions, so runs read as a sequence of outcome lines rather than a transcript of internal mechanics.

## ADDED Requirements

### Requirement: Scan narration is one line per phase on non-TTY output

When stdout is not a terminal, each scan phase SHALL print exactly one line: the completion line naming the input root and the count in user terms (`found 2 video(s) under <root>`, `found 3 picture(s) under <root>`, `found 3 file(s) under <root>`). The scan start line SHALL print only when stdout is a terminal, where it serves as live feedback during long recursive scans. Scan lines SHALL NOT include code-literal qualifiers such as `(recursive=true)`. These defaults apply unless narration is suppressed wholesale by quiet mode (`logging-behavior`).

#### Scenario: Piped scan prints one line

- **WHEN** a video scan over a directory runs with stdout piped
- **THEN** stdout contains one scan line naming the input root and the count, and no separate start line

#### Scenario: Terminal scan keeps its start line

- **WHEN** the same scan runs on a terminal
- **THEN** the start line prints first as live feedback, followed by the completion line

### Requirement: Narration reports outcomes, not mechanics

Console narration SHALL NOT contain lines whose content is purely internal mechanics — concurrency scheduling (`Scheduling N video(s) with max M concurrent encode job(s)...`), batch grouping (`grouping into package batch(es)`), or plan preparation (`preparing pack plan...`). Phase activity is conveyed by progress bars on TTY output; the counts and results are conveyed by the phase's outcome lines.

#### Scenario: Encode batch start prints no concurrency line

- **WHEN** an encode batch starts scheduling work across worker slots
- **THEN** no line naming the concurrent job count prints to stdout

#### Scenario: Picture mode prints no grouping or plan-preparation lines

- **WHEN** a picture compress+pack run progresses from scan to packing
- **THEN** stdout contains no line about grouping into batches or preparing a pack plan

### Requirement: A phase is announced once

A processing phase SHALL NOT announce itself twice: a phase prints either an up-front announcement or its start line, not both (for example, picture compression prints `Compressing 3 picture(s) to JPEG (quality=2)...` and no separate "will be compressed to JPEG" announcement).

#### Scenario: Picture compression has one announcement

- **WHEN** a picture run with compression starts
- **THEN** exactly one line announces the compression phase, naming the count and quality

### Requirement: Narration wording conventions

Trailing status ellipses in narration lines SHALL use ASCII `...`. Report rule lines SHALL use one glyph family (`─`) across all tabular reports (encode plan, organize report); the mid-string filename truncation marker remains `…`, distinct from trailing ellipses.

#### Scenario: Status lines end with ASCII ellipsis

- **WHEN** any narration line ends with an ellipsis
- **THEN** the characters are `...` (three ASCII dots)

#### Scenario: Reports share one rule glyph

- **WHEN** the encode plan and the organize report each render their rule lines
- **THEN** both use the same `─` glyph
