## Why

Probing is deterministic for an unchanged input (same codec, preset, and quality floor produce the same CQ decision), yet every run re-probes every file from scratch — up to 5 CQ points × 4 subprocesses (2 probe encodes + 2 quality scoring passes) per file, with VMAF scoring being the CPU-heavy part. Re-running encro on the same batch of videos wastes minutes of compute on decisions that were already made last run.

## What Changes

- Add a persistent, global probe cache (per-user cache file) that stores probe results keyed by a fingerprint of the input and the decision-affecting configuration.
- On a cache hit, skip the probe phase for that file and use the cached CQ decision directly.
- Mark cached decisions in the printed encoding plan (e.g. `(cached)`) so the user knows the CQ was not re-measured this run.
- Bound the cache: entries carry a timestamp, stale entries (fingerprint mismatch) are skipped, and the cache file is capped with oldest-entry eviction.

## Capabilities

### New Capabilities

- none

### Modified Capabilities

- `video-encode-probing`: probing may be satisfied from a persistent cache for unchanged inputs instead of re-measuring; the encoding plan marks cached decisions.

## Impact

- `src/video/encode_probe.cpp` / `encode_probe.h`: probe phase consults the cache, stores new decisions, exposes a "from cache" marker on `ProbePlan`.
- New cache storage module (read/write/evict the cache file under `%LOCALAPPDATA%/encro/`).
- `printProbePlan` rendering gains the cached marker.
- No CLI changes; no change to the CQ decision algorithm itself (cache stores the exact output of a previous measurement).
