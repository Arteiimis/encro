# Debugging CI failures

Local development is Windows/clang-cl; CI is Linux-only. Everything below was learned the hard way — read this before re-deriving it from a red run.

## Topology

- One workflow: `.github/workflows/ci.yml`, a single job template run as a matrix `{debug, release, coverage}`, all on `ubuntu-24.04`. A full run takes ~17–21 minutes.
- Any branch push triggers CI (only pure-docs path sets are ignored), so pushing a fix branch is the standard way to test without touching `main`.
- CI runs on **origin/main** (or the pushed branch). Before diagnosing, `git fetch` and compare — `git log --oneline origin/main..main` — the failure may already be fixed by unpushed local commits.

## The two incident classes (local green, CI red)

1. **Compiler divergence.** `boost::json::array{boost::json::array{tag, conf}}` builds a *nested* array under clang-cl but a *flat* one under gcc (braced single same-type element → copy constructor wins over `initializer_list`). Symptom: e2e fake-tagger fixtures parse as zero tags on CI only. Fix pattern: build the element as a `json::value` first, then put it in the outer braces. Anything relying on overload resolution subtleties is a candidate for this class.
2. **Tool-version divergence.** The CI image's ffmpeg/ffprobe is older than a local Windows build. Concrete case: ffprobe < 9.0 has no animated-WebP demuxer, so an e2e `packets.size() > 1` assertion failed only on CI. To reproduce locally, use WSL Ubuntu-24.04 and install the CI's ffmpeg version before bisecting encoder vs test.

## Getting the evidence

- Logs: `gh run view <id> --log-failed`; per-job conclusions: `gh run view <id> --json jobs --jq '.jobs[] | {name, conclusion}'`.
- **Artifacts are the fast path.** Every run uploads (with `always()`): per-mode test reports named `test-reports-<mode>` and a run bundle named `ci-run-<ts>-r<run_id>` — download with `gh run download <id> -n <name>` (a `ci-run-*` glob works). e2e failures keep their temp directories, so the artifact contains the failure scratch (`analysis.json`, `fixture.json`, preserved dirs). The boost::json fixture bug above was solved by reading a downloaded `fixture.json`, not by re-reading the source.
- Artifact sub-directories for failing tests use a **hyphen** suffix pattern (`video_encoder_tests-<pid>-…`), not underscores.
- e2e runs redirect encro's logs into a fresh temp dir `encro-e2e-logs-*` (`PrivateLogRootEnv`, `tests/e2e/e2e_test_utils.cpp`). `~/.local/state/encro/logs` is a dead path — a collector pointed there reports empty and proves nothing.
