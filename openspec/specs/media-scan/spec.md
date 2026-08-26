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
