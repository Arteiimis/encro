# Proposal: Add a GCC Toolchain CI Job

## Why

CI currently validates the codebase with a single compiler frontend (clang/clang-cl): the Ubuntu job uses clang-19 and local Windows builds use clang-cl. GCC is the other major C++ frontend and is stricter in several areas the project has never been exercised against. A WSL spike (xmake 3.1.0 + g++-14 + full LTO) already proved the value: it compiled and linked the whole test suite and immediately surfaced 4 real defects — out-of-declaration-order designated initializers that clang silently accepts but GCC rejects per the C++20 standard. Adding a GCC job closes this gap at near-zero cost (g++-14 is already installed on the CI runner).

## What Changes

- Add a Linux GCC-14 CI job that builds and runs the unit + e2e suites in release mode (full LTO).
- Rework the non-Windows toolchain branch in `xmake.lua` so a GCC toolchain is selectable via `xmake f --toolchain=gcc-14`:
  - `set_toolchains("gcc-14")` and `set_toolset("ld", "g++-14")` for the GCC branch (compiler/linker version parity is mandatory for LTO bytecode).
  - Add `-std=c++2c` explicitly for the GCC branch: xmake 3.1.0 release does not map `set_languages("c++26")` to a `-std` flag for GCC, silently leaving gnu++17 (gcc.lua only has the `cxx26` mapping in newer HEAD snapshots).
  - Keep `-fuse-ld=lld` only for the clang branch: GCC full LTO (GIMPLE IR) requires its own driver + bfd + liblto_plugin; under lld, LTO silently degrades or the link fails.
- Fix 4 out-of-order designated initializers found by GCC:
  - `tests/logging_crash_integration_test.cpp:338, 389` — `LogConfig` initializers list `colorsEnabled` before `jsonEnabled`, opposite of declaration order.
  - `src/video/video_encode_runner.cpp:141, 374` — `EncodeConfig` initializers list `crf` before `videoCodec`, opposite of declaration order.
- Existing clang CI jobs remain unchanged.

## Capabilities

### New Capabilities

None — this change is toolchain/CI configuration plus code-correctness fixes with no observable runtime behavior change. Specs are skipped (`skip_specs: true`).

### Modified Capabilities

None.

## Impact

- CI: one additional Linux job (~15 min, free on public repo). No existing job changes.
- Build: `xmake.lua` gains a GCC branch; clang paths are untouched.
- Code: designated-initializer reordering only; no behavior change.
- Local dev: `xmake f -m release --toolchain=gcc-14` becomes a supported local configuration for cross-checking.
