# Tasks: Cross-Platform Build

## 1. xmake.lua 工具链平台分支

- [x] 1.1 将 `set_toolchains("clang-cl")` / `set_toolset("ld", "lld-link")` 包进 `if is_plat("windows")` 分支，Linux 分支显式使用 clang 工具链
- [x] 1.2 Windows 本地重新配置 + 构建 + 跑测试，确认构建行为零变化
- [x] 1.3 验证 clang-18 对 `set_languages("c++26")` 的映射；若不支持，Linux 分支降为 `c++23`（设计 R1）

## 2. GitHub Actions workflow

- [x] 2.1 编写 `.github/workflows/ci.yml`：checkout、setup-xmake、apt 安装 clang-18/g++-14/llvm/ffmpeg（含 update-alternatives 配对 libstdc++-14）
- [x] 2.2 配置 `strategy.matrix.mode: [debug, release, coverage]` 三模式矩阵
- [x] 2.3 配置 `actions/cache` 缓存 `~/.xmake`，key 为 `xmake-${{ matrix.mode }}-${{ hashFiles('xmake.lua') }}`
- [x] 2.4 测试入口按模式分支：debug/release 跑 `xmake run tests` + `xmake run e2e_tests`；coverage 跑 `xmake coverage --summary`
- [x] 2.5 配置 `on: [push, workflow_dispatch]` 与 `concurrency`（同分支取消过期 run）

## 3. 首次 CI run 与 POSIX 编译修复

- [x] 3.1 提交 xmake.lua 分支 + workflow 并 push，触发首个 CI run，收集 POSIX 编译错误清单
- [x] 3.2 解决 clang-18 与 libstdc++-14 的配对（`--gcc-install-dir` 或 `update-alternatives`，设计 R5），确认 `std::print`/`views::enumerate` 可用
- [x] 3.3 迭代修复 `src/` 的 POSIX 编译错误直到 debug 模式全绿
- [x] 3.4 release 模式全绿（含 LTO 链路）
- [x] 3.5 coverage 模式全绿（llvm-profdata/llvm-cov 可用、插桩自检通过、报告生成）
- [x] 3.6 `xmake run e2e_tests` 在 Linux 全绿（stop-signal 测试时序适配如需要，设计 R3）
- [x] 3.7 确认 `[real-ffmpeg]`/`[smoke]` 测试在 Linux 上真正运行而非 SKIP（设计 R4）

## 4. 收尾

- [x] 4.1 三模式矩阵最终全绿确认，无未提交改动
