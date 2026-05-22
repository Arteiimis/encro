# Requirements: Encro — 日志系统优化

**Defined:** 2026-05-23
**Core Value:** 每条日志都能回答三个问题：从哪来的、在干什么、花了多久。

## v1 Requirements

### Infrastructure

日志框架层的核心基础。所有业务代码通过宏与日志系统交互，与 spdlog 具体 API 解耦。

- [ ] **INF-01**: 所有源文件使用 `SPDLOG_INFO/DEBUG/WARN/ERROR` 宏替代 `spdlog::info/debug/warn/error()` 直接调用
- [ ] **INF-02**: 全局启用 `SPDLOG_ACTIVE_LEVEL` 编译期优化（release 模式可剥离 trace/debug）
- [ ] **INF-03**: 定义层级式模块标签命名规范，如 `video.encode`、`video.probe`、`pack.zip`、`picture.compress`
- [ ] **INF-04**: 每个 .cpp 文件通过 `DEFINE_LOGGER("name")` 注册模块 logger，共享文件+控制台 sink
- [ ] **INF-05**: 日志配置与业务逻辑严格分离 — `src/logging/setup.cpp` 负责所有 sink 创建和 logger 注册

### File Management

日志文件生命周期管理：每次运行独立文件，自动清理旧文件。

- [ ] **FILE-01**: 每次运行生成独立日志文件，命名格式 `encro_YYYYMMDD_HHMMSS.log`，位于 `%LOCALAPPDATA%/encro/logs/`
- [ ] **FILE-02**: 启动时自动清理，只保留最近 10 个日志文件
- [ ] **FILE-03**: 日志文件使用 rotating_file_sink_mt，单次运行内按 10 MB 上限自动轮转
- [ ] **FILE-04**: Crash handler 直接文件追加写入当次运行日志文件，绕过 spdlog（防止 shutdown 后日志丢失）
- [ ] **FILE-05**: 日志目录创建失败时 fallback 到临时目录，不阻塞主流程

### Observability

每条日志自带来源信息和耗时，让日志"自解释"。

- [ ] **OBS-01**: 每条日志包含源文件路径和行号（通过 `%@` 或 `%s:%#` 模式标记渲染）
- [ ] **OBS-02**: 每条日志包含模块/组件标签（通过 `%n` 模式标记渲染 named logger 名称）
- [ ] **OBS-03**: pipeline 各阶段（scan → probe → encode → pack）自动记录起止和耗时（RAII ScopedTimer）
- [ ] **OBS-04**: 日志格式清晰可读：`[时间戳] [级别] [模块] [文件:行] 消息 [耗时(可选)]`

### Forensics

出错时提供完整诊断信息，无需复现即可定位问题。

- [ ] **FOR-01**: 出错时自动输出操作链路回溯（处理文件 → 阶段 → 重试次数 → 具体错误）
- [ ] **FOR-02**: 出错时输出环境快照（并发槽位状态、已处理/剩余文件数、FFmpeg 进程状态）
- [ ] **FOR-03**: 错误上下文使用 thread-local RAII 作用域栈，在调用点序列化（不依赖 spdlog MDC）

### Tooling

可选的结构化输出，方便工具链集成。

- [ ] **TOOL-01**: `--log-json` CLI flag，启用 NDJSON 格式（每行一个 JSON 对象）
- [ ] **TOOL-02**: JSON 输出使用自定义 `json_formatter`（实现 `spdlog::formatter`），用 `boost::json` 序列化，确保正确的字符串转义
- [ ] **TOOL-03**: JSON 格式下控制台输出保持人类可读文本格式

## v2 Requirements

Deferred to future release.

- **TOOL-04**: 日志性能基准测试套件（编码吞吐量对比 verbose on/off）
- **TOOL-05**: `--log-level <module>:<level>` 按模块过滤日志级别
- **FOR-04**: 启动时清理遗留的临时 progress 文件

## Out of Scope

| Feature | Reason |
|---------|--------|
| 远程日志/集中式收集 | 纯本地 CLI 工具，不需要网络日志 |
| 实时日志查看 dashboard | 本次聚焦文件日志质量 |
| 日志采样/压缩 | 短生命周期 CLI，日志量有限 |
| spdlog 以外的替代日志库 | spdlog 是最优选择 |
| 二进制日志格式 | 文本格式已满足需求 |
| 动态运行时重配置 | 启动时 CLI flag 配置已足够 |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| INF-01 | Phase 1 | Pending |
| INF-02 | Phase 1 | Pending |
| INF-03 | Phase 1 | Pending |
| INF-04 | Phase 1 | Pending |
| INF-05 | Phase 1 | Pending |
| FILE-01 | Phase 2 | Pending |
| FILE-02 | Phase 2 | Pending |
| FILE-03 | Phase 1 | Pending |
| FILE-04 | Phase 2 | Pending |
| FILE-05 | Phase 1 | Pending |
| OBS-01 | Phase 1 | Pending |
| OBS-02 | Phase 1 | Pending |
| OBS-03 | Phase 2 | Pending |
| OBS-04 | Phase 1 | Pending |
| FOR-01 | Phase 3 | Pending |
| FOR-02 | Phase 3 | Pending |
| FOR-03 | Phase 3 | Pending |
| TOOL-01 | Phase 4 | Pending |
| TOOL-02 | Phase 4 | Pending |
| TOOL-03 | Phase 4 | Pending |

**Coverage:**
- v1 requirements: 20 total
- Mapped to phases: 20
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-23*
*Last updated: 2026-05-23 after initial definition*
