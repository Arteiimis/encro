## MODIFIED Requirements

### Requirement: Compression outputs are cached on disk

The compression step SHALL write its outputs into a cache directory within the hidden `.encro\` directory at the output root (`<work-root>\.encro\compress_q{N}\`), and the cache directory name SHALL encode the compression quality so that different qualities never share a cache. The cache SHALL be preserved when a run is canceled or interrupted, and SHALL be removed when a run completes successfully. A run with `--restart`, or a run whose saved job state does not match the current command, SHALL start from an empty cache.

#### Scenario: Successful run cleans up the cache
- **WHEN** a picture-compression run completes successfully
- **THEN** the cache directory is removed

#### Scenario: Canceled run preserves the cache
- **WHEN** a picture-compression run is canceled during compression
- **THEN** the cache directory and its compressed outputs remain on disk
- **AND** a later run can resume from them

#### Scenario: Different quality does not reuse another quality's cache
- **WHEN** a run with quality q2 is followed by a run with quality q5
- **THEN** the q5 run does not treat q2 outputs as complete
- **AND** all pictures are compressed at q5

#### Scenario: Restart discards the cache
- **WHEN** a picture-compression run is started with `--restart`
- **THEN** all cache directories are removed and compression starts fresh

#### Scenario: Cache lives in the hidden work directory
- **WHEN** a picture-compression run creates its cache
- **THEN** the cache is created at `<work-root>\.encro\compress_q{N}\` inside the dot-prefixed hidden directory
- **AND** `<work-root>` is the output root resolved for the run