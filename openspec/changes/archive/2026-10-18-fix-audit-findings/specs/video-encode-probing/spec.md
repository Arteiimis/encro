# Spec Delta

## ADDED Requirements

### Requirement: A probe cache that cannot be read in full is discarded

The persisted probe cache SHALL never abort a run: a cache file that is unparseable, one whose schema version the build does not know, and one that parses but does not carry its entries array SHALL all be treated as an empty cache. The run SHALL then probe every file and rewrite the cache, and SHALL NOT print a console diagnostic for the discarded file.

#### Scenario: A cache without its entries array is discarded

- **WHEN** the cache file parses as JSON with the current schema version but carries no entries array
- **THEN** loading yields no cached decisions and every file is probed

#### Scenario: A truncated cache file is discarded

- **WHEN** the cache file was left truncated by an interrupted write
- **THEN** loading yields no cached decisions and the run continues

#### Scenario: An unusable cache never fails the run

- **WHEN** the cache file cannot be used for any of those reasons
- **THEN** the run completes normally and rewrites the cache with its fresh decisions
