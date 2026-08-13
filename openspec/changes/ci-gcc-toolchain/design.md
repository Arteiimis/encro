# Design: GCC Toolchain CI Job

## Context

CI (`.github/workflows/ci.yml`) currently runs one `linux` job with a `[debug, release, coverage]` matrix, all on clang-19 + libstdc++-14 + lld. `xmake.lua` hardcodes `set_toolchains("clang")` for non-Windows platforms and force-adds `-fuse-ld=lld`. A WSL spike validated a GCC-14 configuration end to end: full build + full unit suite green, plus 4 real designator-order defects and one xmake flag-mapping gap discovered (see Decisions). The verification environment differs from CI in one way only: WSL used `boost[all] 1.90.0` with `pyver=3.12, cmake=false` to bypass an xmake-repo python 3.14.3 self-test flake that does not reproduce on GitHub runners (existing CI installs the same python package successfully).

## Goals / Non-Goals

**Goals:**

- Add a Linux GCC-14 CI job (release mode) that builds and runs unit + e2e suites with GCC full LTO.
- Make the GCC toolchain selectable via `xmake f --toolchain=gcc-14` without disturbing clang paths.
- Fix the 4 designated-initializer ordering defects GCC found.

**Non-Goals:**

- Windows/macOS CI jobs (future change; macOS still blocked on Apple libc++ lacking `std::views::enumerate` and the brew llvm 21.1.3+ arm64 regression, LLVM issue #165357).
- GCC debug/coverage matrix entries (release exercises the LTO path that differs most from clang; more entries add little signal).
- Structured spec work (no runtime behavior change; `skip_specs: true`).

## Decisions

### D1: GCC-14 via versioned toolchain, not set_toolset

`xmake f --toolchain=gcc` alone is not enough: `set_toolchains("clang")` in `xmake.lua` overrides the command-line toolchain (verified: compile commands still used `/usr/bin/clang`), and `--toolchain=gcc` picks the default gcc 13, which lacks `std::print`/`views::enumerate`. The correct form is the versioned toolchain `xmake f --toolchain=gcc-14`, which xmake ships (`toolchains/gcc-14`) and which packages also inherit — `set_toolset("cxx", "g++-14")` only affects the target, not dependency packages, so packages built with gcc 13 produced LTO 13.1 bytecode that lto1 (14.0) refused to link. xmake.lua gains a branch:

```lua
elseif get_config("toolchain") == "gcc" or get_config("toolchain") == "gcc-14" then
  set_toolchains("gcc-14")
  set_toolset("ld", "g++-14")
  add_cxxflags("-std=c++2c")
```

`get_config("toolchain")` is available at xmake.lua top level and returns the configured toolchain on the final load (verified: branch selected correctly, compile command became `/usr/bin/gcc`/g++-14). `set_toolset("ld", "g++-14")` keeps the link driver at 14 to match compile-side LTO bytecode.

### D2: Explicit -std=c++2c for the GCC branch

xmake 3.1.0 release's `modules/core/tools/gcc.lua` has no `cxx26` mapping (only newer HEAD snapshots do), so `set_languages("c++26")` silently emits no `-std` flag for GCC and the build falls back to gnu++17, failing on `std::expected`. The GCC branch adds `add_cxxflags("-std=c++2c")` (accepted by gcc 14) as a belt-and-suspenders fix that stays harmless once xmake gains the mapping. Alternative considered: pinning setup-xmake to a HEAD snapshot — rejected, CI should track the stable release.

### D3: lld only for clang

GCC full LTO is GIMPLE IR resolved by gcc's driver via liblto_plugin + bfd. Under `-fuse-ld=lld`, fat LTO objects link but LTO silently degrades (or the link fails without fat objects). The GCC branch therefore omits the lld flag and uses the default linker. The clang branch keeps `-fuse-ld=lld` (its thin-LTO + bfd history, per the existing xmake.lua comment).

### D4: CI job shape

A new `linux-gcc` job (separate from the existing `linux` job to keep the coverage `if` logic untangled): single release mode, `xmake f -m release --toolchain=gcc-14`, build `encro encro_e2e_tool tests e2e_tests`, run unit tests with the same JUnit + artifact upload pattern, then e2e. The package cache key gains `runner.os` only if cross-OS jobs arrive; clang/gcc share one key safely because xmake package instances are hashed per toolchain (separate directories under `~/.xmake/packages`).

### D5: Designator fixes are pure reordering

The 4 sites are initialized with identical values; only member order changes to match declaration order (`LogConfig`: jsonEnabled before colorsEnabled; `EncodeConfig`: videoCodec before crf). No behavior change; clang builds are unaffected.

## Risks / Trade-offs

- **gcc-14 full LTO cost**: release job runs longer than debug (~15 min total). Accepted: it is the single most valuable GCC-specific signal (a second, independent LTO implementation).
- **xmake flag-mapping gap may resurface**: if xmake later maps `cxx26` for GCC, the explicit `-std=c++2c` is redundant but harmless (same flag). If gcc 15+ changes the accepted spelling, the branch may need `-std=c++26` — watch compile warnings.
- **Designator reorder risk**: reordering initializer members cannot change semantics (designated initializers are positional-independent by name); risk is limited to typos, covered by the existing suites.
- **python package flake (WSL only)**: CI already builds the python 3.14.3 package successfully today; no CI change needed for it.
