# Tasks: GCC Toolchain CI Job

## 1. Fix designated-initializer ordering (gcc blockers)

- [ ] 1.1 Reorder `LogConfig` initializers in `tests/logging_crash_integration_test.cpp` (lines 338, 389): `jsonEnabled` before `colorsEnabled`, matching declaration order in `src/logging/setup.h`.
- [ ] 1.2 Reorder `EncodeConfig` initializers in `src/video/video_encode_runner.cpp` (lines 141, 374): `videoCodec` before `crf`, matching declaration order in `src/video/encode_config.h`.
- [ ] 1.3 Run the full unit suite (clang) to confirm no behavior change from the reordering.

## 2. xmake.lua GCC branch

- [ ] 2.1 Replace the non-Windows `set_toolchains("clang")` else-branch with a three-way split: windows (clang-cl + lld-link) / gcc (per 2.2) / default clang + `-fuse-ld=lld` (unchanged).
- [ ] 2.2 Implement the GCC branch: `set_toolchains("gcc-14")`, `set_toolset("ld", "g++-14")`, `add_cxxflags("-std=c++2c")`, gated on `get_config("toolchain") == "gcc" or get_config("toolchain") == "gcc-14"`.
- [ ] 2.3 Verify locally (Windows, clang-cl) that the default clang paths are unaffected: `xmake f -m release && xmake build tests` still green.

## 3. CI job

- [ ] 3.1 Add a `linux-gcc` job to `.github/workflows/ci.yml`: checkout, setup-xmake, package cache (same key as linux), setup-linux-env, `xmake f -m release --toolchain=gcc-14 -y`, build `encro encro_e2e_tool tests e2e_tests`, run unit tests with the JUnit/artifact pattern (reuse the linux job's steps, s/linux-gcc/), then `xmake run e2e_tests`.
- [ ] 3.2 Keep `fail-fast: false` and the cache save step so a failing gcc job does not poison the cache (the package cache key stays shared with clang; instances are hashed per toolchain).
- [ ] 3.3 Push and verify the gcc job goes green in CI; confirm the unit-test step log shows `All tests passed`.

## 4. Verification & docs

- [ ] 4.1 Verify in CI that the gcc job's test reports upload on failure (reuse the existing upload step path; spot-check the artifact name is gcc-specific to avoid clobbering the linux job's `test-reports-release`).
- [ ] 4.2 Update `CLAUDE.md` Build section: document `xmake f -m release --toolchain=gcc-14` as a supported local cross-check configuration.
- [ ] 4.3 Run `xmake format -k` and commit the change (implementation + tests + this tasks.md in one commit).
