## MODIFIED Requirements

### Requirement: Probing shows progress feedback

The probe phase SHALL show progress bars in the same style as the encode bars: one bar per worker slot (reused across files, showing the current file, CQ, and sub-step), plus an Overall bar when the batch exceeds the worker count. In the default compact mode per-worker slot bars SHALL NOT be created when the batch holds more than one file, and the Overall bar SHALL appear whenever the batch holds more than one file, so a compact multi-file probe renders the Overall bar alone. A batch whose files are all cache hits creates no slot bars in either mode, because slot bars are sized to the files actually being probed. A single-file batch that is probed keeps one slot bar and no Overall bar in either mode. `--full-progress` SHALL keep the per-worker slot bars and the worker-count Overall threshold. The terminal cursor SHALL be hidden while the bars render and restored afterwards; non-TTY output (pipes, tests, CI) SHALL render no bars.

#### Scenario: Batch probe shows per-slot bars

- **WHEN** the user runs a batch encode with more files than workers and `--full-progress`
- **THEN** the probe phase shows an Overall bar plus one bar per worker slot, and the bars update as each file's probe points complete

#### Scenario: Compact multi-file probe shows the Overall bar alone

- **WHEN** a multi-file probe runs in the default compact mode
- **THEN** the probe phase creates no per-worker slot bars and renders the Overall bar alone, which still advances as each file completes and while files are in flight

#### Scenario: Single-file probe shows one slot bar and no Overall bar

- **WHEN** a probe runs for a single file that is not a cache hit, with or without `--full-progress`
- **THEN** exactly one slot bar is created and no Overall bar is

#### Scenario: No bars on non-TTY output

- **WHEN** output is captured by a pipe or a test harness
- **THEN** no progress bar sequences are emitted
