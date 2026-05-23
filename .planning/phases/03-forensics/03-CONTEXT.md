# Phase 3: Forensics - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning

<domain>
## Phase Boundary

当错误发生时，日志自动包含完整诊断链 —— 正在处理哪个文件、在哪个阶段、第几次重试、具体错误是什么，以及失败时的系统状态快照（并发槽位、剩余队列、FFmpeg 进程）。开发者通过在函数边界放置 `ScopedErrorContext ctx("stage", detail)` 来累积上下文，`LOG_ERROR` 在调用点自动序列化完整链路 —— 无需手动传递上下文或依赖 spdlog MDC。

Phase 1 的宏注入点（D-01）已为此预留扩展空间。Phase 2 的 ScopedTimer 已建立 RAII 阶段边界的代码模式。
</domain>

<decisions>
## Implementation Decisions

### Error Context Threading Model
- **D-01:** 使用 thread-local RAII 作用域栈 —— `thread_local std::vector<ContextFrame>`，每个线程独立。不跨线程共享或传递上下文。完全符合 FOR-03 spec："thread-local RAII 作用域栈，在调用点序列化（不依赖 spdlog MDC）"。
- **D-02:** 上下文深度上限 16 帧 —— 防止深层调用栈无界增长。超出时丢弃最旧帧，附加 `[truncated]` 标记。

### Context Chain Serialization Format
- **D-03:** 上下文以内联方式附加到 LOG_ERROR 消息体中，格式：`"error message [context: file.mkv > encode stage > retry 2/3 > FFmpeg exit code 1]"`。由 LOG_ERROR 宏在 TLS 栈非空时自动追加。人类可读优先；Phase 4 添加 JSON 结构化等价输出。
- **D-04:** 上下文链中每个 ContextFrame 渲染为 `"stage(detail)"` 格式，帧之间用 ` > ` 连接。示例: `"input.mkv > encode(retry 2/3) > ffmpeg(exit 1)"`。

### Environment Snapshot Scope
- **D-05:** 快照包含：活跃槽位数量 + 每个槽位的文件路径、队列中剩余文件数、当前 FFmpeg 子进程 PID 和命令行。由 LOG_ERROR/LOG_CRITICAL 触发，作为单独日志块输出（紧跟错误行之后）。精确匹配 FOR-02 spec。
- **D-06:** 快照通过 `logging::captureEnvironmentSnapshot()` 自由函数获取。访问模式：读取 `AppContext&` 中的 RuntimeContext 和相关状态指针。快速、非阻塞 —— 不获取任何锁（读取原子变量和 immer 结构体）。

### ScopedErrorContext API & Placement
- **D-07:** `ScopedErrorContext` 是 RAII 类 —— 构造函数 `ScopedErrorContext(std::string_view stage, std::string_view detail)` 将帧推入 TLS 栈，析构函数弹出。`noexcept` 析构函数遵循 Phase 2 D-11 先例。放在 `src/logging/logging.h`，与 LOG_* 宏和 ScopedTimer 同级（Phase 1 D-13 模式）。
- **D-08:** 放置位置镜像 ScopedTimer（Phase 2 D-19/D-20）：video.scan → probe → encode → pack，picture.scan → compress → pack，pack.execute。额外放置：encode_runner 和 WebP 自适应编码中的重试循环边界。每个 ScopedErrorContext 推入带有阶段名称 + 详细信息的帧（文件名、尝试次数 N/M）。

### Integration with eh::Result<T>
- **D-09:** `eh::Result<T>` 保持完全不变（`std::expected<T, std::string>`）。上下文仅存在于 TLS 栈中 —— 不通过 Result 类型携带或传播。错误传播和上下文累积之间无耦合。FOR-03 spec："完整的累积链路在作用域内任何 LOG_ERROR 触发时内联序列化，无需手动上下文传递"。

### Claude's Discretion
- **ScopedErrorContext 可移动但不可复制** —— 与 ScopedTimer 的 move-only 语义一致（Phase 2 先例）。移动构造将源对象的 `movedFrom_` 标记为 true，防止双重弹出。
- **快照获取不获取锁** —— `captureEnvironmentSnapshot()` 仅读取原子变量和 `immer::atom` 结构体。在错误路径（可能持有锁）调用是安全的。
- **上下文帧格式** —— stage 和 detail 均为 `std::string_view`，零拷贝。帧内容在日志记录时格式化（不在构造时），因此 string_view 必须在 ScopedErrorContext 生命周期内保持有效。
- **与 ScopedTimer 共存** —— ScopedErrorContext 和 ScopedTimer 是独立的 RAII 类型，可嵌套/重叠。常见的模式是：`ScopedTimer timer("encode"); ScopedErrorContext ctx("encode", filename);` 在同一个作用域中。
- **线程安全** —— TLS 栈按定义是线程安全的，无需互斥锁。`captureEnvironmentSnapshot()` 读取的 immer 结构体是无锁的。整个上下文/快照机制是信号处理器安全的（Phase 2 D-23 禁止在信号处理器中记录日志，但上下文累积在信号处理器触发之前发生）。
</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & Roadmap
- `.planning/ROADMAP.md` — Phase 3 完整定义，3 条 success criteria，依赖 Phase 2
- `.planning/REQUIREMENTS.md` — FOR-01, FOR-02, FOR-03 详细说明
- `.planning/PROJECT.md` — Core Value、Constraints、Key Decisions

### Phase 1 & 2 Foundation (downstream MUST understand what's already built)
- `.planning/phases/01-logging-foundation/01-CONTEXT.md` — Phase 1 全部决策（D-01 宏注入点为 Phase 3 预留，D-13 logging.h 为新增类型预留位置）
- `.planning/phases/02-file-management-observability/02-CONTEXT.md` — Phase 2 全部决策（D-11 noexcept 析构函数，D-19/D-20 阶段定义和 ScopedTimer 放置点，D-23 信号处理器安全约束）
- `.planning/phases/02-file-management-observability/02-04-PLAN.md` — ScopedTimer 管道插桩的完整方案（Phase 3 的 ScopedErrorContext 放置镜像此模式）

### Research (pre-roadmap analysis)
- `.planning/research/PITFALLS.md` — 关键陷阱及预防措施。Phase 3 相关：Pitfall #2（async source_loc 悬垂 — Phase 3 的上下文注入遵循相同的消息体注入模式），Pitfall #10（crash handler + 文件写入 — 已在 Phase 2 解决）

### Codebase Maps
- `.planning/codebase/ARCHITECTURE.md` — 系统架构、组件职责、错误处理策略、数据流
- `.planning/codebase/INTEGRATIONS.md` — 崩溃处理器、线程池、日志集成点
- `.planning/codebase/CONVENTIONS.md` — 命名、代码风格约定

### Key Source Files (Phase 3 modification surface)
- `src/logging/logging.h` — Phase 1 已交付的宏和 DEFINE_LOGGER；Phase 3 在此添加 ScopedErrorContext
- `src/logging/log_tags.h` — 可添加 `infra.forensics` 标签
- `src/video/video_encode_runner.cpp` — `failEncoding()` 和 `encodeWebpWithTargetSize()` 中的重试循环；Phase 3 在此添加上下文守卫和快照触发
- `src/video/video_batch_execution.cpp` — `runEncodingTask()` 中的槽位管理；Phase 3 的快照从此处读取活跃槽位状态
- `src/video/video_process.cpp` — 管道分发和 LOG_ERROR 调用点；Phase 3 在此放置 ScopedErrorContext
- `src/picture/picture_process.cpp` — 图片管道编排；Phase 3 在此放置 ScopedErrorContext
- `src/pack/pack_service.cpp` — Pack 操作编排；Phase 3 在此放置 ScopedErrorContext
- `src/core/error_handle.h` — `eh::Result<T>` 和 `eh::makeError()`；Phase 3 不修改此文件（上下文通过 TLS 栈运行，不通过 Result 类型）
</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **LOG_ERROR 宏注入点（Phase 1 D-01）:** `src/logging/logging.h` —— 自定义 LOG_* 宏已预留用于上下文链注入的扩展点。Phase 3 在 LOG_ERROR 展开中添加 TLS 栈读取 + 环境快照。
- **ScopedTimer RAII 模式（Phase 2）:** `src/logging/logging.h` —— ScopedErrorContext 遵循相同的 move-only、noexcept 析构函数设计。现有的测试模式（`logging_scoped_timer_test.cpp`）可直接适配上下文测试。
- **`EncodingExecutionContext` 槽位管理:** `src/video/video_batch_execution.h:134-275` —— `setActive(slot, state)`、`clearActive(slot)`、`pendingTotal()` 已维护快照所需的所有状态。`captureEnvironmentSnapshot()` 读取这些而不获取锁。
- **`immer::atom` 无锁状态:** `src/core/app_context.h` —— `VideoInfoCacheStore` 使用 `immer::atom<immer::map>` 进行无锁并发读取。快照读取可以遵循相同模式（只读快照，无需互斥锁）。
- **`stopsignal` 原子模式:** `src/infra/stop_signal.cpp` —— 信号处理器安全的原子标志模式。虽然 Phase 3 不在信号处理器中添加日志记录（Phase 2 D-23），但 TLS 栈的 push/pop 在信号中断时本质上是安全的（无堆分配，无锁）。

### Established Patterns
- **East const、trailing return type、匿名命名空间** —— 与 Phase 1/2 相同约定
- **`src/` relative includes** —— `#include "logging/logging.h"`
- **RAII 类 + 自由函数** —— ScopedErrorContext 是 RAII 类（与 ScopedTimer 类似）；`captureEnvironmentSnapshot()` 是自由函数
- **`noexcept` 析构函数** —— 所有 RAII 类型遵循此约定（Phase 2 D-11）
- **线程安全通过设计实现** —— TLS 上下文栈无需互斥锁；immer 读取无需锁；槽位状态通过原子变量读取
- **零开销抽象** —— TLS 栈在无错误路径上除了 push/pop 外不产生额外开销。仅在 LOG_ERROR 触发时进行格式化/快照。

### Integration Points
- **`LOG_ERROR` 宏 → TLS 栈:** `logging.h` 中的宏展开点读取 TLS 上下文栈，如果非空则格式化并内联追加。不改变现有的 LOG_ERROR 调用点。
- **`ScopedErrorContext` → 管道函数:** 在 `video_process.cpp`、`picture_process.cpp`、`pack_service.cpp` 的函数入口处（与 ScopedTimer 位置相同）声明。
- **`captureEnvironmentSnapshot()` → `EncodingExecutionContext`:** 读取活跃槽位状态和待处理队列。需要访问 `AppContext&` 或专门的状态引用。
- **Crash handler 集成:** 崩溃路径在 `crash_runtime.cpp` 中已有自己的回退链（Phase 2 D-14~D-16）。Phase 3 的上下文在崩溃时自动可用 —— TLS 栈在调用 `LOG_CRITICAL` 时仍然有效（崩溃发生在同一线程上，栈未被解开）。无需更改崩溃处理器。
- **测试基础设施:** `tests/logging_scoped_timer_test.cpp` 的模式（RAII 生命周期、嵌套、move 语义、noexcept 保证）可直接适应 ScopedErrorContext 测试。需要额外的集成测试用于快照捕获和上下文链格式化。
</code_context>

<specifics>
## Specific Ideas

无外部参考或"像 X 那样做"的时刻 —— Phase 3 遵循已建立的代码库模式：
- `ScopedErrorContext` 镜像 `ScopedTimer` 的 RAII 设计（Phase 2）
- `captureEnvironmentSnapshot()` 读取现有的 `EncodingExecutionContext` 状态
- 上下文链格式化遵循与 Phase 1 源位置注入相同的消息体注入模式（D-03）
- 线程本地存储是标准 C++ `thread_local` —— 无外部依赖
</specifics>

<deferred>
## Deferred Ideas

无 —— 讨论保持在 phase 范围内。
</deferred>

---
*Phase: 3-Forensics*
*Context gathered: 2026-05-23*
