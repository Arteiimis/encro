# path-identity Specification

## Purpose

Defines when two filesystem paths name the same file, so job-state task records, pack grouping keys and path hashes all decide identity by one rule instead of each site choosing its own.

## Requirements

### Requirement: Path identity follows the filesystem's case rule

Two paths SHALL name the same file exactly when the platform's filesystem treats them as the same file: on a case-insensitive filesystem letter case is ignored, on a case-sensitive filesystem it is significant. Every identity derived from a path — job-state task ids, pack grouping keys and short path hashes — SHALL decide identity by that rule. A case-folded form MAY still order paths, because ordering only needs one stable total order.

#### Scenario: Files that differ only in case are two tasks

- **WHEN** a video scan on a case-sensitive filesystem finds `A.mp4` and `a.mp4` in one directory
- **THEN** two distinct task records exist and both files are encoded
- **AND** an interrupted run that resumes encodes whichever of the two did not finish

#### Scenario: One file spelled two ways is one task

- **WHEN** a run names the same file twice with different letter case on a case-insensitive filesystem
- **THEN** a single task record covers both entries

#### Scenario: Ordering keeps its case fold

- **WHEN** a conflict group of input paths is sorted for conflict-handled naming
- **THEN** the resulting order is identical on both platforms and ignores letter case
