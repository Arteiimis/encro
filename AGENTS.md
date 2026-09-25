# AGENTS.md — encrō

encrō (encro) is a batch media processing CLI on top of ffmpeg: parallel video transcoding (HEVC/H.264), picture → WebP/JPEG recompression, ZIP packing, a resumable job store, and an original-vs-encoded `preview` subcommand.

## Build & Run

- **Build system:** xmake (not CMake). Toolchain: `clang-cl` + `lld-link` on Windows. C++26. For any xmake-related question or change (xmake.lua, configuration, CLI), read the `xmake` skill first.
- **Build:** `xmake build encro` · **Run:** `xmake run encro <args>` (e.g. `xmake run encro -h`; do NOT use `--` — it is passed through to the program and breaks CLI11 parsing)
- **Order-dependent failure:** Catch2 randomises case order, and the failing run's console log names its seed (`Randomness seeded to: <N>`) — reproduce with `build/windows/x64/release/tests.exe --rng-seed <N>`.
- **Real-model smoke:** the `[real-model]` cases read the model directory from `ENCRO_TEST_MODEL_DIR` and `SKIP()` when it is unset, so the fixture carries no machine path (`ENCRO_TEST_MODEL_DIR=<dir> xmake test-report --tag="[real-model]"`).
- **Reporter-mode probe:** `build/windows/x64/release/tests.exe -r console -s` must report 0 failures. `-s` echoes successful assertions, so an assertion inside a redirect window is captured as reporter text and fails a capture assertion — keep assertions outside `testutils::captureStdout` / `FileCapture` windows.
- **Tests with failure summary:** `xmake test-report` — builds + runs unit tests, writes `build/last-test-report.xml` (JUnit) and `build/last-test-console.log`, prints a pass/fail summary instead of raw console. `--tag="[tag]"` filters (the `=` form is required).
- **Tests (e2e):** `xmake build e2e_tests && xmake run e2e_tests` (needs `encro` + the `encro_e2e_tool` fake ffmpeg/ffprobe built first).
- **Tests (parallel):** `xmake test-parallel` shards both suites by case name (`--unit-shards` / `--e2e-shards` override the counts, `--selftest` checks the partitioning helpers on fixtures). A shard's JUnit report decides pass/fail, and a shard that runs fewer cases than assigned fails — so green means every case ran; artifacts land in `build/.test-parallel/`.
- **Tooling tasks:** `fmt` (clang-format) / `tidy` (report-only clang-tidy) / `coverage` / `size` / `include-cleaner` live in `plugins/*/xmake.lua` — `xmake <task> --help` lists the options.
- **ASan:** `xmake f -m releasedbg && xmake build encro` (config then build; `xmake f` alone only reconfigures)
- **Dependency headers:** read `build/compile_commands.json` for absolute include paths — they live there, not in the repo (never search `~/.xmake`).
- **Modes:** `debug` / `release` / `releasedbg` / `coverage`; per-mode flags live in `xmake.lua` (top) and `plugins/coverage`.

## Code Conventions (repo-specific — observed by hand, only layout is tooling-checked)

| Item           | Rule                                                                 |
| -------------- | -------------------------------------------------------------------- |
| East const     | `std::string const&`                                                 |
| Trailing return| Prefix style (`bool f()`) for scalars and `void`; trailing `auto f(...) -> T` for complex/derived types only; lambdas keep an explicit return type when deduction would change it. |
| Naming         | Files `snake_case` · Types `PascalCase` · Functions `camelCase`      |
| Members        | `camelCase` + trailing `_` (e.g., `stateFilePath_`)                  |
| Constants      | `k` + `PascalCase` (e.g., `kEncodeVideoKind`)                        |
| Namespaces     | lowercase, no separators, no indent inside                           |
| Header guards  | `#pragma once` only                                                  |
| Include order  | own header → project headers by module → third-party → stdlib; relative to `src/` |
| Comments       | Minimal, no Doxygen                                         |
| Template params| `Ty` (single), `Tys` (pack)                                          |

## Testing

- **Test-value rules** (a test earns its place by the regression it names, not by the lines it covers):
  - One `TEST_CASE` = one spec scenario or one named failure mode ("if X regresses, this goes red"); no speculative cases for imagined future needs.
  - Assert behavior, not structure — it must go red when the behavior changes and stay green when the implementation is refactored; coverage is a probe, not a target (to raise confidence on changed lines, flip a condition and confirm a test goes red instead of adding cases).
  - Test at the cheapest level that covers the contract — fake-tool unit test by default; e2e only for process boundaries, stop/resume and real-ffmpeg smoke, and never re-asserting what a unit test already covers.
  - Negative paths: one case per equivalence class, never an exhaustive parameter/branch matrix.
  - Reuse the existing fakes (`fake_media_tool`, fake ffmpeg/ffprobe); add no new mocks, and never mock the module under test.
  - Every `TEST_CASE` must assert; an assertion-free case (hidden probe, crash-free smoke) needs a `// assert-ok: <reason>` marker above it — enforced by the `[test-utils][meta]` check.
- Fixtures/helpers in `tests/test_utils.h`. `TempDir` keeps its directory (and prints the path to stderr) when a test fails, so state files / fake-tool logs survive for inspection. E2E subprocess failures dump child stdout/stderr via `REQUIRE_SUCCESS` (in `tests/e2e/e2e_test_utils.h`).
- E2E: `fake_media_tool.cpp` impersonates ffmpeg/ffprobe, controlled via env vars (`ENCRO_FAKE_FFMPEG_EXIT_CODE`, ...).
- `[real-ffmpeg]`/`[smoke]` tests auto-skip via `SKIP()` when ffmpeg not on PATH.
- `[install]`/`[smoke]` completion tests (they write shell startup files / spawn real bash + PowerShell) are opt-in: skipped unless `ENCRO_TEST_COMPLETION=1` (`ENCRO_TEST_COMPLETION=1 xmake test-report --tag="[completion]"`).
- Tests carry tags for `--tag=` filtering — see the `[...]` annotations in `tests/`.
- **Sync convention:** tests synchronize by polling observable state via `testutils::waitUntil` (invocation logs, gate files, mutex-guarded fields) — never fixed sleeps, and no negative assertions that race async effects; elapsed-time tests drive `testutils::ScopedSyntheticJobClock`, not the wall clock. Any `sleep_for` in `tests/**.cpp` needs a `// sleep-ok: <reason>` marker within 3 lines (pre-commit clang-format reflows long lines); the `[test-utils][meta]` check in `tests/test_utils_tests.cpp` enforces both markers. Fake-tool gating lives in `tests/e2e/fake_media_tool.cpp`.

## Communication

- 与用户对话使用中文（代码注释、git 提交、OpenSpec 文档仍为英文）。

## Development Workflows

- **OpenSpec:** features follow proposal → specs → design → tasks → implementation via the `openspec-*` skills (artifacts in `openspec/changes/`). Every skill lives once in `.agents/skills/`, which all harnesses read — never fork a copy per tool. **Before starting any OpenSpec step, read the corresponding skill first (`.agents/skills/openspec-<step>/SKILL.md`) and follow its workflow exactly — never run an OpenSpec step from memory.** Every feature needs ≥1 test; spec documents are written in English (see Communication).
- **OpenSpec explore trigger:** when the user's request is exploratory — "探索下"/"explore", feasibility or options discussion, or similar exploratory intent — automatically start with the `openspec-explore` skill (read `.agents/skills/openspec-explore/SKILL.md` and enter explore mode) instead of answering or implementing directly.
- **OpenSpec review timing:** write all planning artifacts (proposal → specs → design → tasks) before reviewing the proposal against the complete set — a proposal review without its specs/design/tasks cannot validate the contract between them. Then run the planning-artifact stage of the `code-review` skill: one fresh reviewer against the written artifacts (not the author's intent), whose findings and verdicts are recorded in the change's `tasks.md`.
- **OpenSpec archive auto-sync:** when archiving a change whose delta specs are not yet applied to the main specs, run the sync step automatically (inline, as the archive skill prescribes) without prompting; never archive with stale main specs. Skip the sync only when the user explicitly says so. This overrides the archive skill's sync prompt and stays here on purpose — an openspec skill update would drop it from the skill.
- **TDD:** never write implementation before tests; test + implementation go in the same commit.
- **Post-Change Review:** after self-verification, unless trivial (typos, docs-only, one-liner or mechanical refactor), run the `code-review` skill, passing the spec path in as its argument — it runs the code-diff stage's Standards / Spec / Leanness axes and the review fix loop. For large multi-area changes add ≤1 sub-agent per functional area, edge cases only.

## Platform & Git

- **Platform:** primary Windows clang-cl; POSIX paths go through `generic_string()` wherever they are compared or serialized. External ffmpeg/ffprobe come from PATH or `--ffmpeg-path`.
- **Commits:** English only (no CJK in git metadata); conventional commits (`feat:`/`fix:`/`docs:`/`test:`/`refactor:`/`chore:`), subject <72 chars, body wrapped at 80 columns (hard ceiling 90); batch large working trees by functional area.
  - OpenSpec planning artifacts (proposal/specs/design) are committed before implementation, as their own `docs:` commit — they describe what will be built, not the build itself.
  - Implementation + its tests + the change's `tasks.md` checkboxes go in one commit (atomic: `git revert` removes the feature and its completion state together; no "code gone but tasks still checked" intermediate state).
  - A code change and its documentation belong in the same commit when they tell one story; split only when the docs are a prerequisite (planning) or an independent deliverable (user guide).
- **Pre-commit hook:** clang-format on staged C/C++ files (`.githooks/pre-commit`; setup `git config core.hooksPath .githooks`).
