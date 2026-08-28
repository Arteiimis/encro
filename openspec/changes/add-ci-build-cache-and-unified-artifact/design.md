## Context

`.github/workflows/ci.yml` runs one `linux` job with a 3-mode matrix (debug/release/coverage) on fresh runners. Today it caches only `~/.xmake/packages`; `build/.build_cache` (xmake's built-in content-addressed compile cache) is rebuilt from zero per run. Failure evidence is split across `test-reports-<mode>` (failure-only upload, no e2e scratch dirs) and `coverage-report` (always uploaded).

## Goals / Non-Goals

**Goals:**
- Cut per-push rebuild cost via a persisted project compile cache (debug/release modes; coverage disables ccache by design).
- One unified, timestamped, uniquely named artifact per run, available on success and failure, containing every report and the preserved e2e scratch evidence.

**Non-Goals:**
- Caching the whole `build/` directory (unreliable: checkout mtimes defeat xmake's incremental detection; multi-hundred-MB cache churn).
- Uploading binaries/object files (crash stacktraces are already symbolicated into logs; toolchain is scripted so binaries are locally reproducible).
- Deleting the intermediate artifacts after collect (needs `gh api` + token scope; intermediate names stay as the collector's input).

## Decisions

### 1. Cache `build/.build_cache` per matrix mode, keyed on config hash

```yaml
- uses: actions/cache/restore@v5
  id: xmake-ccache
  if: matrix.mode != 'coverage'
  with:
    path: build/.build_cache
    key: xmake-ccache-${{ matrix.mode }}-${{ hashFiles('xmake.lua', 'scripts/setup-linux-env.sh') }}
    restore-keys: xmake-ccache-${{ matrix.mode }}-
# ... build steps ...
- uses: actions/cache/save@v5
  if: always() && matrix.mode != 'coverage' && steps.xmake-ccache.outputs.cache-hit != 'true'
  with: { path: build/.build_cache, key: xmake-ccache-${{ matrix.mode }}-${{ hashFiles('xmake.lua', 'scripts/setup-linux-env.sh') }} }
```

Rationale: xmake's built-in cache is **content-addressed** (source hash + compiler identity + flags), so mtimes don't matter — exactly the property that makes the whole-`build/` approach fail. Per-mode keys avoid concurrent-save collisions on the same cache entry (matrix jobs run in parallel). Hash of `xmake.lua` + toolchain script invalidates on config/compiler changes; `restore-keys` reuses the previous generation for small edits. Skip entirely for coverage mode (`matrix.mode == 'coverage'`) since its instrumentation build disables ccache — the whole `if: matrix.mode != 'coverage'` gate keeps cache traffic off that job. `cache-hit != 'true'` + `always()` mirrors the existing packages-cache save pattern (save even on failed build, avoid duplicate keys).

Alternatives rejected: whole-`build/` cache (mtime trap), remote xmake ccache service (`xmake service --ccache` — needs a hosted server, overkill for one repo).

### 2. Unified artifact via a collect job

```yaml
debug-collect:
  needs: linux
  if: always()   # success AND failure
  runs-on: ubuntu-latest
  steps:
    - name: Timestamp
      id: ts
      run: echo "ts=$(date -u +%Y%m%dT%H%M%SZ)" >> "$GITHUB_OUTPUT"
    - uses: actions/download-artifact@v5
      with:
        path: collect
        pattern: '*'
    - uses: actions/upload-artifact@v5
      with:
        name: ci-run-${{ steps.ts.outputs.ts }}-r${{ github.run_id }}
        path: collect/
        retention-days: 90
```

`download-artifact` with `pattern: '*'` + no `merge-multiple` keeps each origin artifact in its own subdirectory, so the zip never mixes same-named files (each mode's `ut.xml`). The `-r<run_id>` suffix guarantees uniqueness even for same-second runs (GitHub merges same-named artifacts, which would silently concatenate two runs' evidence). Name format matches the spec: `ci-run-<YYYYMMDDTHHMMSSZ>-r<run_id>`.

### 3. Flip report uploads to `always()` and add e2e scratch glob

Matrix jobs' `test-reports-<mode>` upload changes `if: failure() && mode != 'coverage'` → `if: always() && mode != 'coverage'` (one templated step, two effective mode variants), and its `path:` gains `/tmp/video_encoder_tests_*` (star glob; preserved per-failure scratch dirs named `video_encoder_tests_<counter>` — see `tests/test_utils.h`). `if-no-files-found: warn` already set, so a clean run (no scratch dirs) still uploads fine.

Ordering: the upload step must stay after the test steps; `always()` supersedes the existing `failure()` (the comment explaining the `failure()` sequencing stays but is updated). The `Print debug info locations` step keeps its `failure()` gate (it prints job-log guidance only on failure) but its text is updated: the unified artifact name `ci-run-*` becomes the single download target, superseding the per-artifact listing (tasks 2.3).

### 4. Coverage upload unchanged

`coverage-report` keeps its standalone `always()` upload (`build/coverage/html/` + `all.profdata`); the collector picks it up by name. No plugin changes — `xmake coverage` output paths untouched.

## Risks / Trade-offs

- [Cache bloat: `build/.build_cache` grows per mode] → GitHub cache limit is 10 GB/repo; per-mode keys cap individual entries (~100–400 MB predicted); `restore-keys` bounds generations.
- [Collection job adds ~1–2 min per run to CI] → It runs only one lightweight job after the matrix; the unified artifact replaces the need to assemble evidence by hand, which is the point.
- [Stale cache after toolchain drift] → Key hashes `setup-linux-env.sh` + `xmake.lua`, so compiler/flags changes produce a fresh key; `restore-keys` only soft-falls back.
- [Artifact storage growth from always-uploads] → ~1–5 MB per run at 90-day retention; within free quota; binaries explicitly excluded.
- [Zero-delta risk: `debug-collect` with no uploaded artifacts (e.g. all jobs cancelled) would upload an empty zip] → `if-no-files-found: warn` on the upload step; an empty package is harmless and rare (collect runs after the matrix).
- [Cancellation: `concurrency.cancel-in-progress: true` can kill `debug-collect` when a newer push lands on the same ref] → inherent to the existing concurrency model; the superseded run simply lacks a unified zip (its intermediates still exist), and the newer run publishes its own. Accepted; the `Print debug info` step's failure-only guidance still points at intermediates.
- [Scratch glob misses if TMPDIR is set on a runner: `fs::temp_directory_path()` honors TMPDIR, so preserved dirs could land outside `/tmp`] → GitHub ubuntu-latest does not export TMPDIR (scratch lands in `/tmp`); the glob is best-effort with `warn`-on-empty, and task 3.2 verifies the location on a live run.

## Migration Plan

Rollout is the merge itself: cache steps are additive (miss = current behavior), collect job is new, uploads widen from `failure()` to `always()`. Rollback = revert the workflow file; no state, no schema, no data migration.

## Open Questions

None — naming, retention (90 days), and cache key shape are settled in the specs above.