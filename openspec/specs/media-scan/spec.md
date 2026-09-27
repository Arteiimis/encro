# media-scan Specification

## Purpose

Defines how encro scans input media, including which directories are eligible so the scanner never ingests the program's own hidden intermediates as inputs.

## Requirements

### Requirement: Dot-prefixed directories are excluded from scans

Input scans SHALL skip directories whose name begins with a dot (e.g. `.encro`, legacy `.compress_tmp*`), so the program's own hidden working directories are never re-scanned as input media. The exclusion SHALL apply during recursive traversal and MUST NOT silently warn about the excluded directories.

#### Scenario: Recursive scan skips the hidden work directory
- **WHEN** a recursive scan encounters a dot-prefixed directory such as `.encro` or `.compress_tmp_q90` during traversal
- **THEN** the directory and everything below it are skipped
- **AND** no files inside it are reported as input matches
- **AND** no warning is emitted for the skip

#### Scenario: Dot-hidden files are excluded too
- **WHEN** a scan encounters a regular file whose name begins with a dot inside a normal directory
- **THEN** the file is not reported as an input match

### Requirement: Media extension matching is case-insensitive

Media extension matching SHALL compare the extension of a candidate file with the configured extension set using an ASCII case fold (A–Z matches a–z), so an uppercase or mixed-case spelling of a listed extension matches. The comparison SHALL NOT depend on the process locale, and it SHALL remain exact on every other character: a file whose extension is not listed, or differs from a listed extension by anything other than letter case, is not a match. The rule SHALL apply to every scan that matches media extensions, whether the scan root is a directory or a single file.

#### Scenario: Uppercase extension matches a lowercase list

- **WHEN** a scan whose extension set holds `.mp4` meets a file named `CLIP.MP4`
- **THEN** the file is reported as a match

#### Scenario: Mixed-case extension matches a lowercase list

- **WHEN** a scan whose extension set holds `.jpg` meets a file named `PHOTO.Jpg`
- **THEN** the file is reported as a match

#### Scenario: Single-file input follows the same rule

- **WHEN** a scan is given a single file whose extension is an uppercase spelling of a listed extension
- **THEN** that file is reported as a match

#### Scenario: A different extension is still excluded

- **WHEN** a scan whose extension set holds `.mp4` meets a file named `notes.txt`
- **THEN** the file is not reported as a match
