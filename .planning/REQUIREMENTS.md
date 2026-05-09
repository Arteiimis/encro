# Requirements: encro — v1.6 CLI体验增强

**Defined:** 2026-05-09
**Core Value:** Progress visibility — compact single-bar progress by default

## v1.6 Requirements

Requirements for CLI library migration and terminal color deepening.

### CLI11 迁移

- [ ] **CLI11-01**: 26个 option 通过 CLI11 API 定义（add_flag/add_option/add_option_group，4组），cmd.cpp (138行) 完全重写
- [ ] **CLI11-02**: 58处 vm.count()/vm.at() 调用适配为 CLI11 访问模式（config_builder 17 + prelude 2 + terminal 1 + utils 1 + app_entry 2，共 6 consumer files）
- [ ] **CLI11-03**: 自适应列宽保留 — consolewidth::resolveColumns() + resolveHelpTextLayout() 传递到 formatter_fn
- [ ] **CLI11-04**: cmd_cmd_tests.cpp (244行) + cmd_config_builder_tests.cpp (732行) CLI11 测试重写
- [ ] **CLI11-05**: 全部 3033 assertions 零行为回归，所有 CLI flow 等价输出

### CLI着色深化

- [ ] **COLR-01**: --help 输出按 section 着色（Usage/OptionGroup/OptionName/OptionDesc），通过 formatter_fn 注入 terminal::println()
- [ ] **COLR-02**: MessageKind 枚举扩展 5 个新值（Usage/OptionGroup/OptionName/OptionDesc/Version）+ styleFor() 映射
- [ ] **COLR-03**: 错误信息统一使用 terminal::println(Error, ...) / terminal::eprintln()，覆盖解析错误、缺失选项、无效值
- [ ] **COLR-04**: --version 输出着色化
- [ ] **COLR-05**: NO_COLOR 标准（no-color.org）在所有新着色路径中遵守 — colorsEnabled() 门控，piped 输出降级

## v1.5 Requirements (Complete)

Requirements for eliminating picture_process.cpp's pack internal dependencies and extending PackRequest API.

### Naming Strategy (SINK-01)

- [x] **SINK-01**: `NamingStrategy {Flat, FlatWithForce, Keep}` enum in `pack.h` replaces `OutputLayout`+`forceConflictHandling` boolean pair. `NamingConfig` extended with `namingStrategy` field. Internal dispatch uses single-switch. Consumers translate at call site. `AppConfig` fields preserved for CLI parsing.

### Grouping + Summary (SINK-02)

- [x] **SINK-02**: `GroupingStrategy` enum + config and `SummaryConfig` struct (with explicit `isSummary` flag, not prefix convention) added to `PackRequest`. `buildMediaPackPlan` internalizes two-layer logical partitioning behind grouping strategy. Summary entries guaranteed first via structural flag.

### Picture Leak Elimination (SINK-03)

- [x] **SINK-03**: `picture_process.cpp` constructs `PackRequest` with new fields instead of constructing `PackPlan` directly. All 5 internal includes removed (`packer_types.h`, `packer.h`, `pack_internal.h`). Golden zip entry name tests pass with byte-identical output.

### PackPlan Internalization (SINK-04)

- [x] **SINK-04**: `PackPlan` moved from `pack_types.h` to internal header — consumers cannot include it. `static_assert(is_aggregate_v)` removed. All tests pass with zero behavioral change.

## v2 Requirements

(None — all v1.5 features included in scope)

## Out of Scope

| Feature | Reason |
|---------|--------|
| `entryNameForFile` callback deprecation | Still useful for consumer-specific overrides; independent concern |
| New external libraries or build changes | Zero needed — all abstractions use existing C++26 types |
| Picture E2E CLI verification (requires test media + FFmpeg) | Deferred from v1.3, unchanged |

## Traceability

### v1.6

| Requirement | Phase | Status |
|-------------|-------|--------|
| CLI11-01 | Phase 19 | Pending |
| CLI11-02 | Phase 19 | Pending |
| CLI11-03 | Phase 19 | Pending |
| CLI11-04 | Phase 19 | Pending |
| CLI11-05 | Phase 19 | Pending |
| COLR-01 | Phase 20 | Pending |
| COLR-02 | Phase 20 | Pending |
| COLR-03 | Phase 20 | Pending |
| COLR-04 | Phase 20 | Pending |
| COLR-05 | Phase 20 | Pending |

### v1.5 (Complete)

| Requirement | Phase | Status |
|-------------|-------|--------|
| SINK-01 | Phase 15 | Complete |
| SINK-02 | Phase 16 | Complete |
| SINK-03 | Phase 17 | Complete |
| SINK-04 | Phase 18 | Complete |

**Coverage:**
- v1.6 requirements: 10 total
- Mapped to phases: 10
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-09*
*Last updated: 2026-05-09 after v1.6 milestone definition*
