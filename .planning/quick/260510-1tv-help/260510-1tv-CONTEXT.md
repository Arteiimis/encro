# Quick Task 260510-1tv: 优化--help输出颜色区分度和美观度 - Context

**Gathered:** 2026-05-09
**Status:** Ready for planning

<domain>
## Task Boundary

优化 `encro --help` 输出的配色方案，提升颜色区分度和美观度。当前问题：Usage 和 OptionGroup 共用 steel_blue+bold 无法区分，OptionName 的 light_cyan 在亮背景下可见度差，整体配色单调（仅 2 色）。目标：现代 CLI 风格的多色分层方案，保持 OptionDesc 纯文本。

仅涉及 `src/infra/terminal.cpp` 中 `styleFor()` 的 4 个 MessageKind 映射修改（Usage, OptionGroup, OptionName, Version），以及 `tests/cmd_cmd_tests.cpp` 中帮助文本颜色断言适配。不修改 formatter_fn 结构。

</domain>

<decisions>
## Implementation Decisions

### 配色方案选择
- **D-01:** 现代 CLI 多色分层方案 — 4 个元素采用 3 种不同颜色，比 Phase 20 的 steel_blue 二色体系增加层次感。参考 gh CLI（选项名暖色强调）和 docker CLI（高可见度选项名）。
- **D-02:** 具体颜色分配：
  - **Usage** (行首描述行) → `dodger_blue + bold` — 明亮蓝，作为帮助输出开头最醒目的元素，替代 steel_blue
  - **OptionGroup** (分组标题) → `steel_blue` (去掉 bold) — 与 Usage 区分，保留"段落感"但不抢眼
  - **OptionName** (选项名) → `gold` — 暖金色，高可见度（暗/亮终端背景均醒目），用户扫描帮助时最先注意到的元素
  - **OptionDesc** (选项描述) → plain — 保持纯文本，不分散注意力
  - **Version** (--version 输出) → `dodger_blue + bold` — 与 Usage 一致，版本信息突出显示
- **D-03:** OptionDesc 保持 plain — 用户明确偏好。
- **D-04:** ANSI padding rule 保留 — 列宽计算在颜色注入之前完成（Phase 20 D-02），本次仅改颜色值，不触及排版逻辑。

### 不涉及的范围
- formatter_fn 结构不变 — 仅 styleFor() 返回值变化
- 不新增 MessageKind 枚举值
- NO_COLOR 合规 — 所有着色路径已通过 styledText()/colorsEnabled() 门控
</decisions>

<specifics>
## Specific Ideas

- gold 颜色值: `fg(fmt::color::gold)` 或 `fg(fmt::color::golden_rod)` — gold (#FFD700) 较亮，golden_rod (#DAA520) 稍暗。建议 gold，在终端上更醒目。
- dodger_blue (#1E90FF): 比 steel_blue (#4682B4) 更亮更生动，适合作为主要强调色。
- 所有颜色均来自 fmt 内置命名颜色，零新依赖。

</specifics>

<canonical_refs>
## Canonical References

- `src/infra/terminal.cpp:191-208` — styleFor() 当前实现（Phase 20）
- `src/cmd/cmd.cpp:110-158` — makeHelpFormatter() lambda，调用 styledText() 处
- `src/infra/terminal.h` — MessageKind 枚举定义
- Phase 20 CONTEXT.md — D-01/D-02 原始配色决策（本次迭代改进）
</canonical_refs>
