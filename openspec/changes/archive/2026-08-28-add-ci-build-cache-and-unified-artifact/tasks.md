## 1. Build cache

- [ ] 1.1 Add `actions/cache/restore@v5` step for `build/.build_cache` on the linux job, gated `matrix.mode != 'coverage'`, keyed `xmake-ccache-${{ matrix.mode }}-${{ hashFiles('xmake.lua', 'scripts/setup-linux-env.sh') }}` with `restore-keys: xmake-ccache-${{ matrix.mode }}-`
- [ ] 1.2 Add `actions/cache/save@v5` step after the build/test steps, same key, `if: always() && matrix.mode != 'coverage' && steps.xmake-ccache.outputs.cache-hit != 'true'`, mirroring the existing packages-cache save pattern

## 2. Unified artifact

- [ ] 2.1 Change the `test-reports-<mode>` upload `if:` from `failure()` to `always()` (keep the `mode != 'coverage'` exclusion) and add `/tmp/video_encoder_tests_*` to its `path:` list; update the outdated `failure()` comment
- [ ] 2.2 Add `debug-collect` job (`needs: linux`, `if: always()`): timestamp step (`date -u +%Y%m%dT%H%M%SZ`, `$GITHUB_OUTPUT`), `actions/download-artifact@v5` with `path: collect` + `pattern: '*'`, `actions/upload-artifact@v5` with `name: ci-run-${{ steps.ts.outputs.ts }}-r${{ github.run_id }}`, `path: collect/`, `retention-days: 90`, `if-no-files-found: warn`
- [ ] 2.3 Update the `Print debug info locations` step text: the single download command `gh run download <run_id> -n ci-run-*` supersedes the per-artifact listing; keep the coverage-job branch mentioning `coverage-report` is inside the unified artifact

## 3. Verification

- [x] 3.1 Workflow syntax check: `actionlint` on `.github/workflows/ci.yml` (or GitHub's own parse via a dry dispatch) passes
- [x] 3.2 Dispatch a `workflow_dispatch` run with modes `["debug","coverage"]` (or full matrix) and verify: cache saves to GitHub, second run restores with `cache compiling` hits; unified `ci-run-<ts>-r<run_id>` artifact exists on the successful run and contains `test-reports-debug/`, `test-reports-release/` and `coverage-report/` subdirectories
- [x] 3.3 (If feasible) trigger a failing run (e.g. dispatch with a temporary failing test on a branch) and verify the unified artifact still appears with the preserved e2e scratch dirs inside `test-reports-<mode>/`