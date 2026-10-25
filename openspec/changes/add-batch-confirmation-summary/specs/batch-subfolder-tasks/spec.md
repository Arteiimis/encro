# Spec Delta

## ADDED Requirements

### Requirement: A batch states its scope before its single confirmation

A batch that will ask a confirmation SHALL print one scope block on stdout before the first subfolder starts work: a header naming the processable subfolders out of the enumerated ones and the total entry count, one indented row per processable subfolder carrying that subfolder's entry count, and a statement that the confirmation below covers the whole batch. A subfolder with no processable media SHALL NOT get a row. The block SHALL NOT print when the run's narration is suppressed.

#### Scenario: Scope block precedes the first subfolder's work

- **WHEN** a batch over subfolders `a/` (12 pictures), `b/` (8 pictures) and `c/` (no media) is about to ask the packing confirmation
- **THEN** stdout carries one block before any subfolder's own `Found ...` line, its header names 2 of 3 subfolders and 20 `picture(s)`, its rows name `a/` with 12 and `b/` with 8, and its closing statement says the confirmation covers the whole batch

#### Scenario: The confirmation keeps its text and position

- **WHEN** the same batch reaches the packing confirmation
- **THEN** the question text ending `(Y/n): ` is byte-identical to the single-directory question and prints once, after the block
- **AND** the block adds no prompt line of its own, and carries no severity prefix or badge

#### Scenario: A media-less subfolder is absent from the block

- **WHEN** subfolder `c/` has no processable media
- **THEN** the block has no row for it, and the run still prints its `Skipping <path>: no processable media.` notice at `c/`'s turn

### Requirement: Scope-block counts use the run's own noun

The count's noun SHALL follow the run's work: `video(s)` in a video run, `picture(s)` in a picture run that does not convert videos, and `item(s)` in a picture run with video conversion enabled, where the pictures and the convertible videos share one number.

#### Scenario: Video run counts videos

- **WHEN** a `--type video` batch prints its block
- **THEN** the header and rows count `video(s)`

#### Scenario: Picture run with video conversion counts items

- **WHEN** a `--type picture --video-webp` batch prints its block
- **THEN** the header and rows count `item(s)`, and a subfolder holding only videos still gets a row

### Requirement: Scope-block counts are scan-time values

The block SHALL report the counts the run's scans find, and SHALL NOT state how many entries will be encoded, converted or kept. A count can therefore exceed what a phase's `Found <n> ...` line reports, because the pipeline's own video scan drops an already-encoded HEVC file before that line prints.

#### Scenario: Counts are scanned entries, not a work promise

- **WHEN** a video subfolder holds videos that the later scan and probe drop (already-encoded HEVC files, or a probe estimating an output larger than the source)
- **THEN** the block still reports the scanned count it printed before the run, and no line of the block states how many entries will be encoded or kept

### Requirement: Runs that ask nothing or stay quiet print no scope block

A batch SHALL print no scope block for a `--yes`, `--pack-only`, `--dry-run` or `--quiet` run, for a batch whose subfolders all have no processable media, and for a batch whose stop request arrived before the first subfolder.

#### Scenario: --yes prints no block

- **WHEN** the batch runs with `--yes`
- **THEN** no scope block and no confirmation print, and every processable subfolder proceeds

#### Scenario: Pack-only prints no block

- **WHEN** a `--type video --pack-only` batch runs
- **THEN** no scope block prints and each subfolder is packed as before

#### Scenario: Dry run prints no block

- **WHEN** a picture batch runs with `--dry-run`
- **THEN** no scope block prints and each subfolder takes its existing dry-run path

#### Scenario: Quiet mode prints no block

- **WHEN** the batch runs with `--quiet`
- **THEN** no scope block prints, and the confirmation prompt still appears with its unchanged text

#### Scenario: Nothing processable prints no block

- **WHEN** every enumerated subfolder has no processable media
- **THEN** no scope block prints, every subfolder is skipped with its notice, and the summary table reports the run as today

#### Scenario: A stop that already arrived prints no block

- **WHEN** a stop request arrives before the first subfolder starts work
- **THEN** no scope block prints, and every subfolder is skipped as a stop exactly as before
