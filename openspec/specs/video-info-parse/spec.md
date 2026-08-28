# video-info-parse Specification

## Purpose

Defines the error-handling contract for parsing numeric fields out of ffprobe JSON output: unparseable values degrade to "missing" (triggering the documented fallback or error path) rather than throwing, so media metadata reads never surface as exceptions.

## Requirements

### Requirement: Probe JSON numeric fields never throw

Numeric metadata fields read from ffprobe output (`nb_frames`, `duration`, and frame-rate values) SHALL be parsed defensively: any value that is not a canonical number SHALL be treated as absent, feeding the caller's existing fallback or error handling. A malformed field value MUST NOT propagate an exception out of the metadata-reading entry points (`getVidTotalFrames`, `getVidTotalDurationUs`); those entry points SHALL always return their `Result` type, either with a computed value, a fallback-derived value, or an error describing the failed retrieval.

#### Scenario: Non-numeric nb_frames falls back instead of throwing

- **WHEN** the probe JSON contains an `nb_frames` string that is not a canonical integer (e.g. `"abc"`) and the stream also carries a parseable `avg_frame_rate` and a `format.duration`
- **THEN** `getVidTotalFrames` returns a frame count estimated from duration and rate, and does not throw

#### Scenario: Non-numeric nb_frames with no fallback data errors cleanly

- **WHEN** the probe JSON contains a non-numeric `nb_frames` string and neither a parseable rate nor a parseable duration is available
- **THEN** `getVidTotalFrames` returns an error result ("failed to retrieve total frames") and does not throw

#### Scenario: Existing guarded parses keep their behavior

- **WHEN** the probe JSON carries values such as `"N/A"`, an empty string, a zero-denominator frame rate, or a non-numeric `duration`
- **THEN** the parse helpers treat them as absent exactly as before this change (fallback or error, never an exception)
