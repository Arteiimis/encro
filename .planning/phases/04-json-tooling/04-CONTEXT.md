# Phase 4: JSON Tooling - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning

<domain>
## Phase Boundary

通过 `--log-json` CLI flag 启用 NDJSON 结构化日志输出。相同日志内容，两种格式：控制台保持人类可读文本，文件以每行一个 JSON 对象输出，供日志分析器和 CI pipeline 程序化消费。

Phase 1-3 的日志基础设施（宏、标签、ScopedTimer、ScopedErrorContext、环境快照）已全部就位 —— Phase 4 仅添加格式层，不改变任何日志调用点或消息内容。
</domain>

<decisions>
## Implementation Decisions

### JSON Formatter Implementation
- **D-01:** 自定义 `spdlog::formatter` 子类（`JsonFormatter`），实现 `format()` 虚函数。使用 `boost::json::object` 构建 JSON，通过 `boost::json::serialize()` 输出单行字符串。完全符合 TOOL-02 spec："使用 `boost::json` 序列化，确保正确的字符串转义"。
- **D-02:** JSON 字段从 `spdlog::details::log_msg` 结构体直接读取 —— 不通过正则解析已格式化的文本。`msg.time`（时间戳）、`msg.level`（级别）、`msg.logger_name`（模块标签）、`msg.source`（源位置）、`msg.payload`（消息体）。

### Dual Sink / Dual File Architecture
- **D-03:** `--log-json` 启用时创建 companion `.ndjson` 文件：与人类可读 `.log` 文件同名、不同扩展名。例如 `encro_20260523_143052.log` + `encro_20260523_143052.ndjson`。人类可读 `.log` 文件和 console 输出保持不变（TOOL-03）。
- **D-04:** 实现方案：添加第二个 `rotating_file_sink_mt`，专门绑定 `JsonFormatter`。现有的 `stdout_color_sink_mt` 和人类可读 file sink 保持现有 pattern formatter。spdlog 原生支持 per-sink formatter —— `sink->set_formatter(std::make_unique<JsonFormatter>())`。

### JSON Schema / Field Design
- **D-05:** 每行 NDJSON 固定字段：`timestamp`（ISO 8601 字符串）、`level`（"trace"~"critical" 小写字符串）、`module`（dot-notation 标签）、`source`（"file.cpp:128" 格式）、`message`（去除 context 后缀的净消息体）。
- **D-06:** 可选字段（存在时包含）：`elapsed_ms`（整数毫秒，从 ScopedTimer 的 "completed in Xms" 消息中解析）、`error_context`（字符串数组，从 Phase 3 的 " [context: ...]" 后缀中解析转换）。

### CLI Flag & Config Chain
- **D-07:** 在 `LogConfig` 中添加 `bool jsonEnabled{false}` 字段。CLI11 在 `cmd/cmd.cpp` 中解析 `--log-json` flag。传递链：`cmd::commandLineInit()` → `CmdParseResult` → `cmd::buildConfig()` → `AppConfig` → `prelude::initStartup()` → `logging::setup(LogConfig)`。
- **D-08:** `--log-json` 独立于 `--verbose`。可以只启用 `--log-json`（生成 JSON 文件，无 console 输出），也可以组合 `--verbose --log-json`（同时生成人类可读和 JSON 文件 + console 输出）。

### Edge Case Handling
- **D-09:** JSON 字符串转义由 `boost::json::serialize()` 自动处理 —— 覆盖 Windows 路径反斜杠、CJK Unicode、嵌入式双引号、换行符。TOOL-03 spec 中所有 edge case 由 `boost::json` 按 JSON 规范保证正确处理。
- **D-10:** 空消息、空模块标签、缺失源位置：JSON 字段使用空字符串 `""` 或 `null`（`std::optional` 字段）。NDJSON 每行必须包含所有固定字段 —— 不因 edge case 省略字段。

### Phase 3 Integration
- **D-11:** `error_context` 字段来源：`JsonFormatter::format()` 检查 `msg.payload` 是否以 `" [context: ...]"` 模式结尾。如果匹配：提取 `> ` 分隔的帧列表，填充 `error_context` 数组；从 `message` 字段中移除后缀。如果不匹配：省略 `error_context` 字段，保留完整 `message`。
- **D-12:** Phase 3 环境快照（`captureEnvironmentSnapshot()`）输出单独的 LOG_INFO 行 —— 在 JSON 中作为独立 NDJSON 行自然呈现，无需特殊处理。

### Log Retention
- **D-13:** `.ndjson` 文件匹配现有的 `encro_*.log*` 清理模式 —— Phase 2 的 `retainRecentLogs()` 需要扩展为同时清理 `encro_*.ndjson*` 和 `encro_*.ndjson.*` 文件。在清理逻辑中添加第二个 glob 模式。

### Claude's Discretion
- **`elapsed_ms` 解析方式:** 从 `msg.payload` 中的 `"completed in Xms"` 模式提取（ScopedTimer 析构函数的标准格式）。不添加额外的结构体侧信道 —— 简单 regex 提取，未匹配时省略字段。
- **`JsonFormatter` 文件位置:** `src/logging/json_formatter.h` —— 独立头文件，与 `logging.h` 分离。`setup.cpp` 在 `--log-json` 时引用。
- **NDJSON 文件名:** 简单替换 `.log` → `.ndjson`（`fs::path::replace_extension(".ndjson")`）。保留原始时间戳前缀。
- **性能考量:** `boost::json::serialize()` 在 async spdlog worker 线程中调用 —— 不阻塞主线程。单行 JSON 序列化开销远低于 FFmpeg 编码，不产生可测量的吞吐量影响。
- **Rotating sink 一致性:** JSON file sink 使用与人类可读 sink 相同的 10MB/3 旋转配置。两者的旋转独立触发 —— 不保证逐行对齐，但这不影响消费（NDJSON 行是自包含的）。
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/ROADMAP.md` — Phase 4 完整定义，3 条 success criteria，依赖 Phase 3
- `.planning/REQUIREMENTS.md` — TOOL-01, TOOL-02, TOOL-03 详细说明
- `.planning/PROJECT.md` — Core Value, Constraints, Key Decisions

### Phase 1-3 Foundation (downstream MUST understand what's already built)
- `.planning/phases/01-logging-foundation/01-CONTEXT.md` — D-01 宏注入点、D-10 日志格式模式、D-12 setup.cpp 集中式配置
- `.planning/phases/02-file-management-observability/02-CONTEXT.md` — D-01~D-06 文件命名和清理、D-08/D-10 ScopedTimer 和 elapsed_ms 格式、D-17 旋转 sink 配置
- `.planning/phases/03-forensics/03-CONTEXT.md` — D-03/D-04 上下文链序列化格式（JSON formatter 需要从中解析 `error_context`）

### Research
- `.planning/research/STACK.md` — boost::json 版本兼容性和 spdlog formatter API
- `.planning/research/PITFALLS.md` — Pitfall #1 (macro vs function)、Pitfall #2 (async source_loc)

### Codebase Maps
- `.planning/codebase/ARCHITECTURE.md` — 日志系统架构、sink 配置
- `.planning/codebase/INTEGRATIONS.md` — boost::json 现有使用点、spdlog 版本

### Key Source Files (Phase 4 modification surface)
- `src/logging/setup.cpp` — 现有 sink 创建（line 174-226）；Phase 4 添加 JSON file sink + JsonFormatter 绑定
- `src/logging/setup.h` — 现有 `LogConfig` struct；Phase 4 添加 `jsonEnabled` 字段
- `src/cmd/cmd.cpp` — CLI11 参数解析；Phase 4 添加 `--log-json` flag
- `src/cmd/config_builder.cpp` — 将 `CmdParseResult` 转换为 `AppConfig`；Phase 4 传递 `jsonEnabled`
- `src/app/prelude.cpp` — 构建 `LogConfig` 并调用 `logging::setup()`；Phase 4 传递 `jsonEnabled`
- `src/core/app_context.h` — `AppConfig` struct；Phase 4 可能需要添加 `jsonEnabled`
- `tests/logging_json_test.cpp` — 新建：JSON 格式化、edge case 转义、NDJSON 行完整性测试
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **spdlog custom formatter API:** spdlog 原生支持 `sink->set_formatter(std::make_unique<CustomFormatter>())` —— 每个 sink 独立的 formatter。无需修改 spdlog 源码或 logger 注册逻辑。
- **`boost::json::object` + `boost::json::serialize()`:** 项目中已有 3 处使用（`src/core/app_context.h:28`, `src/core/job_state.cpp:20`, `src/video/video_info.cpp`）。NDJSON 序列化遵循相同的现有模式。
- **Existing dual-sink architecture:** `setup.cpp` 已经创建 `vector<spdlog::sink_ptr>` 并共享给所有 logger。添加第三个 sink（JSON file）只需在 vector 中追加。
- **`LogConfig` struct (`setup.h:10-15`):** 已有 `verboseEnabled`, `verboseEchoEnabled`, `colorsEnabled`, `customLogDir`。添加 `jsonEnabled` 遵循现有模式。
- **`kLogPattern` (`setup.cpp:73`):** 现有格式字符串 `"[%Y-%m-%dT%H:%M:%S.%e%z] [%^%l%$] [%n] %v"` —— 人类可读 sink 保持不变。JsonFormatter 直接读取 `log_msg` 结构体字段，不依赖 pattern。

### Established Patterns
- **East const, trailing return type, anonymous namespaces** — 与 Phase 1-3 相同约定
- **`src/` relative includes** — `#include "logging/json_formatter.h"`
- **Free functions + RAII classes** — JsonFormatter 是一个实现 `spdlog::formatter` 接口的类
- **CLI11 flag → CmdParseResult → AppConfig → LogConfig 传递链** —— 遵循现有的 `--verbose` / `--verbose-echo` 模式
- **`eh::Result<T>` error handling** —— `logging::setup()` 返回 `std::optional<fs::path>`；formatter 创建不需要 error handling（纯格式化）

### Integration Points
- **`logging::setup()` → sink 创建循环:** 在 `setup.cpp:174-226` 的 sink 创建逻辑中添加条件分支：如果 `config.jsonEnabled`，创建额外的 JSON file sink。
- **`cmd::commandLineInit()` → CLI11:** 在 `cmd.cpp` 中添加 `app.add_flag("--log-json", parseResult.jsonEnabled, "...")`。
- **`retainRecentLogs()` → 清理模式:** 在 `setup.cpp` 中添加第二个 glob：`encro_*.ndjson` + `encro_*.ndjson.*`。
- **测试框架:** `tests/logging_json_test.cpp`（新建）—— 需要注册一个 `ostream_sink` + `JsonFormatter` 的测试 logger，验证 JSON 输出的字段完整性、edge case 转义、NDJSON 行完整性。可复用 Phase 2/3 的 `registerCapturingLoggerForTimer` 测试模式。
</code_context>

<specifics>
## Specific Ideas

无外部参考 —— Phase 4 遵循标准 NDJSON 约定和现有代码库模式：
- JSON 字段命名：`snake_case`（与现有代码库风格一致）
- NDJSON 格式：每行一个完整 JSON 对象，无行内换行
- `spdlog::formatter` 接口：`format()` 接收 `log_msg&`，写入 `memory_buf_t&`
- boost::json 转义：由库自动处理，不自行实现转义逻辑
</specifics>

<deferred>
## Deferred Ideas

### Reviewed Todos (not folded)
无 —— discussion 保持在 phase 范围内。
</deferred>

---
*Phase: 4-JSON Tooling*
*Context gathered: 2026-05-23*
