# Design: Cross-Platform Build

## Context

当前约束（详见 proposal.md）：`xmake.lua` 硬编码 `clang-cl`/`lld-link`；`src/` 有 8 个文件含未验证的 POSIX `#else` 分支；无 CI；仓库已推送至 GitHub（Arteiimis/encro）。两个硬事实决定设计：

1. **编译器下限**：项目实际使用 `std::print`（progress.cpp ×2）、`std::views::enumerate`（packer.cpp ×1）、`std::expected`（error_handle.h，全错误体系）——前两者需要 libstdc++-14（gcc 14+）。ubuntu-latest (24.04) 默认 gcc-13/libstdc++-13 编不过。
2. **coverage 插件是 llvm 专属**：`plugins/coverage/xmake.lua` 写死 `LLVM_PROFILE_FILE`、`llvm-profdata`、`llvm-cov` 和 `-fprofile-instr-generate` 插桩自检。

## Goals / Non-Goals

**Goals:**
- ubuntu-latest 上 debug / release / coverage 三模式构建 + 测试全绿
- Windows 构建行为零变化（本地验证不受影响）
- CI 依赖编译成本可控（缓存，避免每次 push 重编 boost[all]）

**Non-Goals:**
- macOS job（AppleClang 的 C++26 支持最差，后置）
- Windows CI job（本地已充分验证）
- releasedbg/ASan job（一行矩阵改动可随时加入，后置）
- 测试假工具迁移（`consolidate-fake-toolchain` 独立 change；37 个 `.cmd` 测试保持 Windows 门控）
- POSIX 分支的运行时行为调优——只在"编译错误"范围内修复，行为差异留给后续真实测试暴露后再处理

## Decisions

### D1: Linux 统一 clang-18，而非 gcc-14

coverage 插件为 llvm 专属，Linux 统一 clang 则插件、xmake.lua 的 coverage flags、插桩自检全部零改动；gcc 方案需要 xmake.lua flags 分支（`--coverage`）+ 插件改造（llvm-cov → gcovr/lcov），引入第二套报告流程。

代价：clang-18 默认配对 libstdc++-13（无 `std::print`/`views::enumerate`）。缓解：安装 `g++-14`（提供 libstdc++-14-dev），让 clang 指向 gcc-14 的 libstdc++（`--gcc-install-dir` 或 `update-alternatives`，实现时以 CI 实测为准，二选一即可）。

### D2: 三模式矩阵 + 分 mode 缓存 key

`strategy.matrix.mode: [debug, release, coverage]`。`~/.xmake` 包产物按 mode 隔离，缓存 key 必须带 mode：`xmake-${{ matrix.mode }}-${{ hashFiles('xmake.lua') }}`。首次 push 三 job 并行、缓存全 miss，各编译一份 boost[all]（预估 15-30min/job，一次性成本）；此后缓存命中、秒级恢复。

### D3: 测试入口按模式分支

- debug/release：`xmake run tests` + `xmake run e2e_tests`
- coverage：`xmake coverage --summary`（插件自建 coverage 配置并跑单元测试）

### D4: 触发与并发

`on: [push, workflow_dispatch]`（单人 worktree 流程，PR 暂不需要）；`concurrency: group=${{ github.ref }}` + `cancel-in-progress: true` 取消同分支过期 run。

### D5: 工具链平台分支写法

`xmake.lua` 中 `set_toolchains`/`set_toolset` 包进 `if is_plat("windows")` 分支，Linux 走默认 clang（或显式 `set_toolchains("clang")`）；`set_languages("c++26")` 保持全局，clang-18 的映射在实现时验证（见 R1）。

### D6: CI 环境包

`clang-18`、`g++-14`（libstdc++-14）、`llvm`（llvm-profdata/llvm-cov 与 clang-18 同版本）、`ffmpeg`（解锁 `[real-ffmpeg]`/`[smoke]` 测试——Linux 上真实 ffmpeg 回归网，Windows 本地一直 SKIP）。

## Risks / Trade-offs

- **[R1] clang-18 的 `-std=c++26` 映射不完整** → 项目实际最高特性是 C++23（`std::print`/`enumerate`/`expected`），若 clang-18 不接受 c++26 flag，把 Linux 分支的 `set_languages` 降为 `c++23`，零代码影响。
- **[R2] 首次 CI 成本高**（3× boost 编译并行） → 一次性；缓存命中后正常；若可接受度差，可先只跑 debug 一个 job 预热缓存。
- **[R3] e2e stop-signal 测试在无 TTY runner 上行为差异**（`consoleCtrlEventsAvailable` Linux 恒 true；kill SIGINT vs CTRL_BREAK 时序） → 首个 CI 绿前可能需要调整这些测试的时序/断言，不改生产行为。
- **[R4] 真实 ffmpeg 行为差异暴露 Windows 测不到的问题** → 预期内的好事，但第一个绿可能多修几轮；限制在编译与测试适配范围，不扩展到行为重构。
- **[R5] libstdc++ 配对机制（`--gcc-install-dir` vs `update-alternatives`）在 xmake 配置里的具体写法** → 实现时二选一实测，不影响任务分解。

## Migration Plan

无部署/回滚概念（构建工具链 + CI）。实施顺序：

1. xmake.lua 工具链分支（Windows 本地先验证零变化）
2. 提交 workflow + push → 首个 CI run（预期红：编译错误清单）
3. 按清单迭代修复 POSIX 编译错误 → 绿
4. 三模式全绿后，本 change 完成；测试迁移 change 的拆门依赖此处的 Linux 构建能力

## Open Questions

无。R1/R5 的细节均为实现时可实测的选项，不改变规格、方案或任务划分。
