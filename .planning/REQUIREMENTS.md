# Requirements: encro v1.4 Pack接口简化

**Defined:** 2026-04-30
**Core Value:** Pack模块接口由调用方手动编排进化为声明式单一入口，内部细节不外泄

## v1.4 Requirements

Requirements for the pack module interface simplification milestone.

### 1. 单一入口 API

- [ ] **SIMPLIFY-01**: 设计 PackRequest 输入类型，覆盖现有三种调用路径的需求（picture pack、video pack、pack-only）
- [ ] **SIMPLIFY-02**: pack::execute(PackRequest) 单一入口，内部完成分组、命名、Plan构建、执行全流程
- [ ] **SIMPLIFY-03**: PackPlan 不再对外暴露（降级为模块内部实现细节）
- [ ] **SIMPLIFY-04**: pack::detail:: 命名空间类型不再被模块外部消费者 include

### 2. 内部分组与命名统一

- [ ] **SIMPLIFY-05**: Picture 和 Video 统一使用两层切分策略（groupPackEntriesWithSubparts），消除单层/双层分叉
- [ ] **SIMPLIFY-06**: zip 文件命名规则内化到 pack 模块，调用方通过命名偏好参数控制
- [ ] **SIMPLIFY-07**: zip 条目命名（flat vs keep、冲突处理）由 pack 内部完成
- [ ] **SIMPLIFY-08**: 预留自定义命名策略钩子（类型擦除），当前无消费者使用

### 3. 配置集中注入

- [ ] **SIMPLIFY-09**: compact 统一从 AppConfig.fullProgress 推导（修复 Picture 路径硬编码 compact=true）
- [ ] **SIMPLIFY-10**: maxParallelJobs、outputDir、forceNameConflictHandling 等配置从 AppConfig 统一注入 PackRequest

### 4. 行为保持 & 测试

- [ ] **SIMPLIFY-11**: 所有现有 945 断言保持绿，零行为变化
- [ ] **SIMPLIFY-13**: 恢复性执行（jobState / selectPackPlanIndexes）逻辑完全不变
- [ ] **SIMPLIFY-14**: 同名 zip 条目冲突处理逻辑不变（uniqueifyZipEntryNames / forceNameConflictHandling）

### 5. 移除不必要的抽象

- [ ] **SIMPLIFY-15**: 移除 IPacker 抽象基类（`src/pack/ipacker.h`），Packer 不再继承任何抽象基类
- [ ] **SIMPLIFY-16**: PackService 直接持有 Packer（移除 `unique_ptr<IPacker>` 间接层）
- [ ] **SIMPLIFY-17**: MockPacker（`tests/packer_mock.h`）删除，mock 测试改写为真实 Packer + TempDir 集成测试

## Future Requirements

Deferred to later milestones.

- zip 命名策略插件化（如支持用户自定义命名模板）
- pack 格式扩展（tar / 7z）
- PackRequest builder pattern 语法糖

## Out of Scope

| Feature | Reason |
|---------|--------|
| 新打包格式（tar/7z） | 仅重构接口，不新增功能 |
| C++20 modules | clang-cl 尚不支持 MSVC-ABI 模块 |
| PackRequest 链式 builder | 当前需求用 designated initializer 即可；需要时再加 |
| 修改 zip 条目内容逻辑 | 仅重组代码结构，不改 zip I/O 行为 |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| SIMPLIFY-01 | Phase 12 | Pending |
| SIMPLIFY-02 | Phase 12 | Pending |
| SIMPLIFY-03 | Phase 12 | Pending |
| SIMPLIFY-04 | Phase 12 | Pending |
| SIMPLIFY-09 | Phase 12 | Pending |
| SIMPLIFY-10 | Phase 12 | Pending |
| SIMPLIFY-05 | Phase 13 | Pending |
| SIMPLIFY-06 | Phase 13 | Pending |
| SIMPLIFY-07 | Phase 13 | Pending |
| SIMPLIFY-08 | Phase 13 | Pending |
| SIMPLIFY-15 | Phase 14 | Pending |
| SIMPLIFY-16 | Phase 14 | Pending |
| SIMPLIFY-17 | Phase 14 | Pending |
| SIMPLIFY-11 | Phase 14 | Pending |
| SIMPLIFY-13 | Phase 14 | Pending |
| SIMPLIFY-14 | Phase 14 | Pending |

**Coverage:**
- v1.4 requirements: 17 total
- Mapped: 17
- Unmapped: 0

---

*Requirements defined: 2026-04-30*
