# Add CI Build Cache and Unified Artifact

## Why

The CI pipeline (`.github/workflows/ci.yml`) rebuilds all ~97 project translation units from scratch on every push across three parallel matrix jobs (debug/release/coverage): only third-party packages (`~/.xmake/packages`) are cached, while xmake's own content-addressed compile cache (`build/.build_cache`, ~137 MB locally) dies with each ephemeral runner. On failure, investigation material is scattered across two artifact names (`test-reports-<mode>` × 2, `coverage-report`) plus the losing e2e failure scratch dirs, which are printed to stderr and then destroyed with the runner — the highest-value evidence is the one thing never collected.

## What Changes

- **Add a compile cache for project builds**: cache `build/.build_cache` (xmake's built-in content-addressed ccache, enabled by default; only the `coverage` mode disables it, so the cache covers debug/release). Key per matrix mode with `xmake.lua` + toolchain-setup hash, `restore-keys` prefix for partial hits on small config changes, explicit `cache/save` after build (job-failure-safe). Cache miss degrades to the current full build — no risk.
- **Publish one unified run artifact**: a new `debug-collect` job (`needs: linux`, runs on every run outcome) downloads all intermediate artifacts (`test-reports-<mode>`, `coverage-report`), packs them into a single timestamped artifact `ci-run-<ts>-r<run_id>` (UTC timestamp + run id suffix for uniqueness), and uploads it with `retention-days`. Downloaded as one zip; inner layout keeps per-origin subdirectories so same-named files (ut.xml) never collide.
- **Capture e2e failure scratch dirs**: the `test-reports-<mode>` upload (matrix jobs, now on `always()` instead of `failure()`) additionally includes `/tmp/video_encoder_tests_*` (the preserved-per-failure scratch dirs) via glob, `if-no-files-found: warn`.
- **Intermediate artifacts stay as-is** (they feed the collector); binaries, object files, and the compile cache are NOT uploaded — crash stacktraces are already symbolicated into logs by `infra/crash_runtime`, and the toolchain is scripted (`setup-linux-env.sh`) so binaries are reproducible locally at the same commit.

## Capabilities

### New Capabilities

- `ci-artifacts`: Behavior contract for what the CI pipeline publishes per run — one unified `ci-run-*` artifact on every run outcome containing the per-mode test reports (JUnit, console log, encro runtime logs, preserved e2e scratch dirs) and the coverage report, with a unique timestamped name.

### Modified Capabilities

- `coverage-report`: The CI publishing requirement is extended — the coverage HTML report and merged profdata still upload as the `coverage-report` artifact, and additionally land inside the unified `ci-run-*` artifact on every run (not just failures).

## Impact

- **Workflow**: `.github/workflows/ci.yml` — add cache restore/save steps around `build/.build_cache`, flip `failure()` → `always()` on the two `test-reports-*` upload steps + extend their `path` glob, add the `debug-collect` job (timestamp step + `download-artifact` + `upload-artifact`).
- **No application code changes**; `xmake.lua` and `scripts/setup-linux-env.sh` untouched (cache keys hash them, but they are not modified).
- **Specs**: new delta `specs/ci-artifacts/spec.md`, modified delta `specs/coverage-report/spec.md`.
- **Cost**: GitHub cache quota ~100–400 MB per mode key; artifact storage ~1–5 MB per run (90-day retention).