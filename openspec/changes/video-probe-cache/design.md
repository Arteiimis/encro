## Context

Probing is deterministic per (input, settings) pair — see proposal.md - Why — but every run re-measures from scratch: up to 5 CQ points × 4 subprocesses (2 probe encodes + 2 quality scoring passes) per file. The probe decision (`ProbePlan`) currently exists only in memory and is recomputed on every run. The CQ decision algorithm (`decideCq`, metric fallback, floor logic) is unchanged by this change; we only persist and reuse its output.

## Goals / Non-Goals

**Goals:**
- Reuse the previous CQ decision for unchanged inputs so repeated runs skip the probe phase.
- Mark reused decisions visibly in the printed plan.
- Keep the cache safe under config changes, file changes, and cache-file corruption.

**Non-Goals:**
- Changing the probe algorithm, window selection, or metric fallback.
- Cross-machine or shared caches (cache is per-user local).
- Caching ffprobe/`videoInfoCache` data (already in-memory only; out of scope).
- LRU/pinned entries or manual cache management UI — fixed cap with oldest-first eviction is enough.

## Decisions

### D1: Cache file lives in the encro data directory as JSON

`probe-cache.json` in the same per-user data directory that the logs use, resolved by reusing the existing log-dir resolution chain (`resolveCommonLogDir().parent_path()` in `src/logging/setup.cpp` — LOCALAPPDATA → APPDATA → XDG_STATE_HOME → HOME → temp) rather than re-deriving a parallel chain, so the cache never silently lands in temp on fallback platforms. A flat JSON object maps cache keys to decisions. Alternatives rejected: job-state file (semantics are per-job progress; `mergeTasks` clears execution state on fingerprint change — wrong lifecycle), sidecar next to the input (pollutes user directories), in-memory only (does not survive the run — the whole point).

### D2: Cache key = resolved decision inputs, not raw config

Key components: input path, file size, mtime (ms), configured video codec, resolved nvenc preset, resolved maxrate kbps, `--min-vmaf` floor, and the metric that was used (VMAF vs SSIM fallback — HDR inputs fall back, so the metric is part of the decision). "Resolved" applies to preset/maxrate: they come from `resolveInputEncodeSettings` (inferred from resolution when not configured), so the key must use the resolved values or an unconfigured preset would alias across resolution changes. The codec is plain config (`AppConfig.videoCodec`), not inferred. Size+mtime guard content changes; the resolved settings guard decision changes.

### D2a: Cached payload = the full ProbePlan decision

Each cached entry stores everything a plan row needs: chosenCq, p5, estimatedBytes, metric, and unreachableFloor — the same fields `collectPlanStats`/`printProbePlan` render (CQ, p5, est. size, ratio, totals, warnings). Storing the full decision keeps cached rows identical to fresh rows; a CQ-only payload would silently degrade plan output. Entries with `probed == false` (short videos, measurement failures — default CQ, no measurement) are never written to the cache, so a cached entry always represents a real measurement.

### D3: Validation and eviction

On load: parse failure or schema-version mismatch discards the whole file (re-probe everything, rewrite later). Per entry: mismatch on any key component → treated as a miss (the probe runs and overwrites the entry). Cap: max ~2000 entries; on insert beyond cap, drop the oldest `updatedAt` entries. No TTL — staleness is handled by the key components.

### D4: Single writer, end-of-phase flush

Probe workers run concurrently, but instead of locking the cache file mid-phase, decisions accumulate in the existing `plans` vector and are written once after `runProbePhase` completes (plus a read of the file once at phase start). This avoids inter-thread file I/O entirely; the phase is the natural transaction boundary. A crashed run simply loses that run's new decisions (worst case: next run re-probes).

### D5: Cached marker flows through ProbePlan

`ProbePlan` gains a `fromCache` flag; `printProbePlan` renders `(cached)` next to the reused CQ. The flag is also visible in logs. The confirmation prompt and dry-run/`--yes` paths are unchanged — a cached decision is a decision like any other.

## Risks / Trade-offs

- [mtime granularity / touch rewrites] → Only a cache miss; re-probe cost, no wrong behavior. Conservative direction is fine.
- [Size+mtime unchanged but content actually changed] → Rare; the CQ decision would be from the old content. Impact is at most one CQ step of difference in quality — within probe sampling noise; accepted.
- [ffmpeg version upgrade changes encoder efficiency] → Cached decisions are from the old build. Accepted: schema version guards encro's own format, not ffmpeg's behavior; a version key would invalidate the cache on every ffmpeg update for marginal accuracy.
- [Cache file grows unbounded] → Fixed entry cap with oldest-first eviction bounds size; the file is flat JSON, typically < 1 MB at cap.
- [Corrupt cache file] → Parse failure discards the file and re-probes; never blocks the run.

## Migration Plan

The cache file is additive: first run after this change writes a fresh cache; no existing state file or CLI compatibility is affected. Rollback = revert the change; a leftover cache file is ignored by the older build (unknown file in the data directory).

## Open Questions

None — key composition, validation, and lifecycle are settled above.
