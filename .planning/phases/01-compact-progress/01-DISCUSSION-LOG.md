# Phase 1: Compact Progress Mode - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-26
**Phase:** 01-compact-progress
**Areas discussed:** 编码+打包一体化, 激活方式, 适用范围与显示内容

---

## 编码+打包一体化

| Option | Description | Selected |
|--------|-------------|----------|
| 单条连续, 阶段加权 | One bar 0-100%, encoding ~85% packing ~15%, phase label changes | |
| 单条连续, 均分权重 | One bar, encoding 0→50%, packing 50→100% | |
| 单条连续, 按文件比例 | Encoding/packing proportional to file and archive counts | |
| **两条独立进度条** | One encoding bar + one packing bar, sequential, no per-worker bars | ✓ |

**User's choice:** Two independent progress bars — encoding then packing. Same sequential order as existing. No per-worker bars.

### Encoding bar content

| Option | Description | Selected |
|--------|-------------|----------|
| Overall计数 + 当前文件名 | "Encoding: 3/15" + current file name, always shown | |
| 仅百分比 | "Encoding: 45%" | |
| **Overall计数（现有风格）** | "Overall: 3/15" same as existing overall bar style | ✓ |

**User's choice:** Keep existing overall bar style "Overall: X/Y" with percentage. Always show it (remove guard condition).

### Packing bar content

| Option | Description | Selected |
|--------|-------------|----------|
| **单条打包进度, overall风格** | "Packing: 1/5" mirroring encoding bar style | ✓ |
| 单条打包进度, 百分比 | "Packing: 60%" only | |

**User's choice:** Single packing overall bar, "Packing: X/Y" style.

### User prompts during encode+pack

| Option | Description | Selected |
|--------|-------------|----------|
| **保留所有交互** | Keep "do you want to encode...?" prompt and final summary unchanged | ✓ |
| 交互也精简 | Skip confirmation prompt in compact mode, like --yes-to-all | |

**User's choice:** Keep all user interaction prompts unchanged. Only progress bars change.

---

## 激活方式

| Option | Description | Selected |
|--------|-------------|----------|
| CLI标志 --compact-progress | New flag to enable compact mode | |
| 自动检测终端宽度 | Auto-enable when terminal < 80 columns | |
| CLI标志 + 自动回退 | Flag + auto fallback for narrow terminals | |
| **紧凑模式为默认, --full-progress恢复** | Compact is default, no flag needed. --full-progress restores old behavior | ✓ |

**User's choice:** Compact progress is the **default**. `--full-progress` restores old multi-bar behavior.

### --compact-progress vs --verbose-echo

| Option | Description | Selected |
|--------|-------------|----------|
| **两者独立，--verbose-echo优先** | Both can be set, verbose-echo wins. No error. | ✓ |
| 互斥，同时使用报错 | Error if both flags set | |

**User's choice:** Independent flags. `--verbose-echo` takes priority when both are set.

---

## 适用范围与显示内容

| Option | Description | Selected |
|--------|-------------|----------|
| 仅视频编码+打包流程 | Only video encoding + pack path affected | |
| **所有进度条场景** | All scenarios: video encode, picture compress, pack-only, encode+pack | ✓ |

**User's choice:** Compact mode applies to all progress bar scenarios.

### Single-file encoding

| Option | Description | Selected |
|--------|-------------|----------|
| **单文件保持现有行为** | Single-file encoding keeps detailed frame/time progress bar | ✓ |
| 单文件也统一为overall风格 | Single file uses "Overall: 1/1" style too | |

**User's choice:** Single-file encoding unchanged — retain detailed progress bar.

---

## the agent's Discretion

- Exact percentage calculation for packing overall bar (file-count weighted vs. archive count)
- Whether picture compression already uses overall bars or needs adaptation
- How `taskexec::TaskPlan` / `TaskContext` are modified for overall-bar-only packing
- Internal refactoring approach (`compact` parameter on `EncodingProgressState`, `PackPlan`, `runEncodingTasks`)

## Deferred Ideas

None — discussion stayed within phase scope.
