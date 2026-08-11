# Cross-Platform Build

## Why

encro 的目标是全面跨平台，但当前只能在 Windows 上构建：`xmake.lua` 硬编码 `set_toolchains("clang-cl")` + `set_toolset("ld", "lld-link")`，Linux/macOS 上无法配置。代码中的 POSIX 分支（`stop_signal`、`crash_runtime`、`terminal` 等 14 处 `#else` 分支）认真写过但从未编译验证，仓库也没有任何 CI。缺少 Linux 构建验证手段是跨平台之路的第一块绊脚石。

## What Changes

- `xmake.lua` 工具链按平台分支：Windows 保持 `clang-cl` + `lld-link`（行为零变化）；Linux 使用 `clang`。
- 新增 GitHub Actions workflow（`.github/workflows/ci.yml`）：ubuntu-latest 上以矩阵方式跑 **debug / release / coverage** 三个构建模式。
- 新增 CI 基础设施：`actions/cache` 缓存 `~/.xmake`（按 mode + xmake.lua hash 分 key，避免重复编译 boost[all]）、`concurrency` 取消同分支过期 run、`workflow_dispatch` 手动触发。
- CI 环境安装 `clang-18` + `g++-14`（提供 libstdc++-14，满足 `std::print`/`std::views::enumerate`）、`llvm`（llvm-profdata/llvm-cov，coverage 插件依赖）、`ffmpeg`（解锁 `[real-ffmpeg]`/`[smoke]` 测试）。
- 修复 POSIX 编译错误：未验证的 `#else` 分支首次编译必然暴露问题，以 CI 红→绿的循环收敛；修复不改动运行时行为。
- 测试入口：debug/release 跑 `xmake run tests` + `xmake run e2e_tests`；coverage 跑 `xmake coverage --summary`（coverage 插件为 llvm 专属，因此 Linux 统一 clang 工具链）。

## Capabilities

### New Capabilities

无。本 change 为纯构建/CI 工具链改动，不改变任何运行时行为，已通过 `.openspec.yaml` 的 `skip_specs: true` 显式跳过 specs。

### Modified Capabilities

无。

## Impact

- **构建配置**：`xmake.lua`（工具链平台分支，Windows 行为不变）。
- **新增文件**：`.github/workflows/ci.yml`。
- **源代码**：仅修复 POSIX 分支的编译错误（预期少量、局部、不改变行为）；`src/` 的 Windows 专属代码（console_width、crash handler 等）保持原样。
- **依赖**：CI 运行时新增 clang-18/g++-14/llvm/ffmpeg（仅 CI 环境，项目依赖不变）。
- **不涉及**：测试的假工具迁移（`consolidate-fake-toolchain` 为独立 change，37 个 `.cmd` 测试仍保持 Windows 门控，拆门依赖本 change 的 Linux 构建能力）。
- **前置**：代码已推送至 `https://github.com/Arteiimis/encro`（私人仓库）。
