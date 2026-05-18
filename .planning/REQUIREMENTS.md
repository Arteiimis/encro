# Requirements: encro

**Defined:** 2026-05-19
**Core Value:** 可预测性 — 用户在执行前能完整预览将要发生的操作

## v1.7 Requirements

Requirements for `--dry-run` option and pre-flight validation.

### --dry-run 预检 & 预览

- [ ] **DRYR-01**: `--dry-run` flag 注册到 CLI11（General group），解析后路由到 dry-run 路径，跳过编码/压缩/打包/job state 写入
- [ ] **DRYR-02**: Validation 层 — 检查 ffmpeg/ffprobe 路径与版本、input path 存在可读、output parent 可写、参数组合无冲突，逐项输出 [OK]/[WARN]/[FAIL]
- [ ] **DRYR-03**: Scan 层 — 完整递归文件扫描（复用 media::scanByExtensions），输出文件数量与总大小；`--resume` 时读取已有 job state 展示已完成/待处理计数，不修改 job state
- [ ] **DRYR-04**: Plan 层 — 展示编码文件数、目标格式、worker 数、预估打包 archive 数；输出目录已存在则提示可能覆盖
- [ ] **DRYR-05**: 三层递进式输出格式 — Validation → Scan → Plan，每层失败则后续层跳过；dry-run 默认输出全貌无需 --verbose 叠加
- [ ] **DRYR-06**: dry-run 流程复用正常运行的校验逻辑（工具链解析、配置合法性），新增检查（输出可写性、job state 可读性）提取为独立可复用函数

## Out of Scope

| Feature | Reason |
|---------|--------|
| 编码时间估算的精确计时 | 扫描阶段做粗略估算即可，精确到秒不是 dry-run 的核心价值 |
| 磁盘空间检查 | 复杂且不可靠（不同文件系统、压缩前后差异大）；Plan 层展示预估大小已足够 |
| --dry-run 在 pack-only 模式下的特殊行为 | 三种模式（video/picture/pack-only）共用同一套 Validation+Scan+Plan 框架 |

## Traceability

| Requirement | Status |
|-------------|--------|
| DRYR-01 | Pending |
| DRYR-02 | Pending |
| DRYR-03 | Pending |
| DRYR-04 | Pending |
| DRYR-05 | Pending |
| DRYR-06 | Pending |

**Coverage:** 6 requirements, 0 unmapped

---
*Requirements defined: 2026-05-19*
*Last updated: 2026-05-19*
