# Phase 12: PackRequest 声明式 API & 配置注入 - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-30
**Phase:** 12-packrequest-api
**Areas discussed:** PackRequest 结构设计, execute() 函数签名, 配置注入策略, PackPlan 内部化与恢复性执行

---

## PackRequest 结构设计

| Option | Description | Selected |
|--------|-------------|----------|
| 单一结构体 + optional 字段 | 一个 PackRequest 包含所有可能字段，按需赋值，遵循 designated initializer | ✓ |
| 按 mode 区分的 variant/tag | std::variant<PicturePackRequest, VideoPackRequest, DirectoryPackRequest> | |
| Builder 模式 | PackRequest::builder().entries(...).build() 链式 | |

| Option (entries 类型) | Description | Selected |
|--------|-------------|----------|
| std::vector<fs::path> | 最简形式，附加信息从 filesystem 内部推导 | ✓ |
| 保留 PackEntryInput | 扩展为统一输入类型 | |
| 允许两种类型 | entries + sourceDir 同时支持 | |

| Option (mode 区分) | Description | Selected |
|--------|-------------|----------|
| 需要 mode 枚举 | PackRequest 含 Media / Directory 枚举 | ✓ |
| 从其他字段推导 | 有 entries=文件打包，有 sourceDir=目录扫描 | |
| 完全由 Phase 13 处理 | PackRequest 只带文件列表 | |

| Option (命名配置) | Description | Selected |
|--------|-------------|----------|
| NamingConfig 子结构体 | 可选子结构，含 layout + forceConflictHandling | ✓ |
| 平铺到顶层 | 直接 PackRequest.layout / .forceConflictHandling | |
| 由 mode 决定 | 模块内部自动选择，无需消费者指定 | |

---

## execute() 函数签名

| Option | Description | Selected |
|--------|-------------|----------|
| 自由函数 pack::execute() | PackRequest 自包含，模块内部构造 Packer | ✓ |
| PackService 实例方法 | 保留现有架构 | |
| execute() + AppConfig | 分离 CLI 配置和打包参数 | |

| Option (jobState) | Description | Selected |
|--------|-------------|----------|
| PackRequest 含 jobState 字段 | 可选指针，非空时启用恢复性执行 | ✓ |
| jobState 作为第二参数 | pack::execute(req, &store) | |
| 模块内部完全处理 | 自动检测 workdir 下 state 文件 | |

| Option (返回值) | Description | Selected |
|--------|-------------|----------|
| eh::Result<PackRunResult> | 保留现有类型，含 exit code + zip 路径列表 | ✓ |
| eh::Result<vector<fs::path>> | 只返回路径 | |
| eh::Result<int> | 只返回 exit code | |

| Option (声明位置) | Description | Selected |
|--------|-------------|----------|
| 新增 pack.h | 唯一公开头文件，仅含 PackRequest + execute() | ✓ |
| 放在 pack_service.h | 最小改动 | |
| 放在 pack_types.h | 与类型定义在一起 | |

---

## 配置注入策略

| Option (compact) | Description | Selected |
|--------|-------------|----------|
| PackRequest 显式携带 compact bool | 消费者从 fullProgress 推导，统一行为 | ✓ |
| PackRequest 携带 fullProgress | execute() 内部计算 compact | |
| 内部读取全局配置 | 违反显式原则 | |

| Option (outputDir) | Description | Selected |
|--------|-------------|----------|
| PackRequest 必需字段 | 调用方解析后传入，execute() 不推导 | ✓ |
| 可选字段 + 模块默认 | 未设置时自动推导 | |
| PackRequest 不包含 | 保持各调用方自行处理 | |

| Option (maxParallelJobs) | Description | Selected |
|--------|-------------|----------|
| optional + 模块默认 | 未设置时 execute() 调用 resolveWorkerCount() | ✓ |
| 必需字段 | 调用方必须计算好再传入 | |
| 硬编码 | 不暴露，始终用 resolveWorkerCount() | |

---

## PackPlan 内部化与恢复性执行

| Option (恢复逻辑位置) | Description | Selected |
|--------|-------------|----------|
| 完全内部化到 pack 模块 | execute() 内部处理 mergeTasks + needsExecution + 状态回调 | ✓ |
| 保持 archive_plan.cpp 但操作 PackRequest | 关注点分离但需暴露内部过滤能力 | |
| 消费者自行处理 | 调用前自行过滤已完成文件 | |

| Option (archive_plan.cpp) | Description | Selected |
|--------|-------------|----------|
| 删除 | 全部逻辑移入 pack 模块，PreparedPackExecution 删除 | ✓ |
| 大幅简化 | 保留为薄层，仅构建 TaskRecords + 管理状态机 | |
| 保持不变 | 仅改参数类型 | |

| Option (静态方法) | Description | Selected |
|--------|-------------|----------|
| Phase 12 降级为 private/free function | 移除 public 声明，命名调用移入 execute() 内部 | ✓ |
| 保持 public 到 Phase 13 | 减少 Phase 12 范围 | |
| 直接删除 | Phase 12 内部重实现 | |

---

## the agent's Discretion

- PackRequest 其余 minor 字段（如 removeOnFailure）由实现阶段确定
- pack.h 的具体 include 结构和 PackRequest 字段声明顺序
- execute() 内部 Packer/PackService 的构造方式

## Deferred Ideas

- zip 命名策略完整内部化 → Phase 13
- 分组策略统一为两层切分 → Phase 13
- IPacker 移除 + MockPacker 删除 → Phase 14
- PackRequest builder pattern 语法糖 → Out of Scope
