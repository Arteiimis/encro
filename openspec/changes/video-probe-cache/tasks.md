## 1. Cache storage module

- [ ] 1.1 Write failing unit tests for the probe cache store: key round-trip, full payload round-trip (chosenCq, p5, estimatedBytes, metric, unreachableFloor), missing file, corrupt JSON (discard + re-probe), schema-version mismatch (discard), entry cap with oldest-first eviction
- [ ] 1.2 Implement the cache store module (load/save flat JSON in the encro data directory resolved via the existing log-dir chain, version stamp, cap ~2000 entries, oldest-first eviction)

## 2. Cache key and probe integration

- [ ] 2.1 Write failing tests for cache-key construction from (path, size, mtime, resolved codec, resolved preset, resolved maxrate, min-vmaf floor, metric)
- [ ] 2.2 Implement key construction using the resolved encode settings (post-`resolveInputEncodeSettings`), not raw config
- [ ] 2.3 Write failing integration tests for cache wiring: hit skips probing, miss probes and records, `probed == false` plans are never written, cache entries restore the full plan payload (CQ, p5, est. size, ratio render identically to fresh)
- [ ] 2.4 Implement the wiring in `runProbePhase`: load once at phase start, skip probing for hits, record decisions, flush once after the phase completes (single writer, end-of-phase transaction)
- [ ] 2.5 Add `fromCache` to `ProbePlan`; tests assert cached plans are marked and logged

## 3. Plan rendering

- [ ] 3.1 Update `printProbePlan` to render `(cached)` next to reused CQ decisions and note the reused count in the preamble; add/extend tests for the rendered output
- [ ] 3.2 Verify `--dry-run` and `--yes` paths treat cached decisions identically to fresh ones (integration test with the fake tool)

## 4. End-to-end verification

- [ ] 4.1 E2E test: run the same batch twice with the fake tool; second run must skip the probe phase (assert probe subprocess count), and the plan must mark decisions as cached
- [ ] 4.2 E2E test: touch/modify an input file between runs; second run must re-probe that file
- [ ] 4.3 Full verification: build, unit + e2e suites, `xmake test-report`
