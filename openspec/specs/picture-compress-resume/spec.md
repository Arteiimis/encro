# picture-compress-resume Specification

## Purpose

Lets interrupted picture-compression runs resume from cached compression outputs instead of recompressing every picture, with cache validity guarded against quality changes and replaced source files.

## Requirements

### Requirement: Compression outputs are cached on disk

The compression step SHALL write its outputs into a cache directory below the output directory, and the cache directory name SHALL encode the compression quality so that different qualities never share a cache. The cache SHALL be preserved when a run is canceled or interrupted, and SHALL be removed when a run completes successfully. A run with `--restart`, or a run whose saved job state does not match the current command, SHALL start from an empty cache.

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

### Requirement: Resume skips already-compressed pictures

When a picture-compression run resumes (saved job state exists and matches the current command), pictures whose compressed output is present in the matching cache SHALL NOT be compressed again. If the saved state does not match, or no saved state exists, the cache SHALL NOT be trusted and compression SHALL start fresh.

#### Scenario: Partial run resumes compression
- **WHEN** a run was canceled after compressing some pictures and the same command is run again
- **THEN** only the pictures without a cached output are compressed
- **AND** the previously compressed pictures are packed from the cache

#### Scenario: Missing state file invalidates the cache
- **WHEN** a cache directory exists but no saved job state exists for the command
- **THEN** the cache is discarded and all pictures are compressed

#### Scenario: Changed input invalidates the cache
- **WHEN** a saved state exists but its config does not match the current command (e.g., different input paths)
- **THEN** the cache is discarded and compression starts fresh

### Requirement: Compression outputs are atomic

A compression output SHALL be considered complete only when its producer process exited successfully and the output was finalized. A partially written output (producer killed or crashed mid-write) SHALL NOT be treated as a valid cached output, and SHALL NOT be packed.

#### Scenario: Interrupted compression leaves no valid output
- **WHEN** the compression process for a picture is killed while writing
- **THEN** no valid output exists for that picture
- **AND** the picture is compressed again on a later resume run
- **AND** the partial output is never packed

### Requirement: Cached outputs are invalidated when the source changes

A cached output SHALL be reused only while the source picture it was produced from is unchanged. If the source file was modified or replaced after the cached output was produced, the picture SHALL be compressed again.

#### Scenario: Replaced source file is recompressed
- **WHEN** a picture was compressed, its source file was then replaced (same path, new content), and the run resumes
- **THEN** the picture is compressed again
- **AND** the archive containing it is produced from the fresh output

### Requirement: Packing decides per entry between compressed and original

The packing step SHALL decide for each picture, based on the files on disk, which file to pack: the cached compressed output when it exists and is not larger than the original, the original picture when the cached output is larger, and nothing at all when no valid cached output exists (compression failed).

#### Scenario: Compressed output is smaller
- **WHEN** a picture's cached compressed output exists and is smaller than the original
- **THEN** the compressed output is packed with a .jpg entry name

#### Scenario: Compressed output is larger than the original
- **WHEN** a picture's cached compressed output exists but is larger than the original
- **THEN** the original picture is packed with its original entry name

#### Scenario: Compression failed
- **WHEN** a picture has no valid cached output after the compression batch
- **THEN** the picture is not packed
- **AND** the run reports the failure

#### Scenario: All compressions fail
- **WHEN** every picture fails to compress
- **THEN** the run fails with an error and does not pack
