#pragma once

// ── 模块标签常量 ────────────────────────────────────────────────────────────
// 层级 dot-notation: component.subcomponent
// 用法: DEFINE_LOGGER(logtags::VIDEO_ENCODE)
//
// 命名约定:
//   - 全小写
//   - 分隔符: 点号 (.)
//   - 层级最多 3 层
//   - 禁止 ad-hoc 字符串 —— 所有 DEFINE_LOGGER 必须引用此处的常量
//
// Phase 1 定义的标签 (当前 src/ 下所有有 spdlog 调用的模块)
// Phase 2-4 新模块的标签由各自 phase 添加 (D-08)

namespace logtags {

// ── app ──
inline constexpr auto APP_ENTRY    = "app.entry";
inline constexpr auto APP_PRELUDE  = "app.prelude";
inline constexpr auto APP_PIPELINE = "app.pipeline";

// ── cmd ──
inline constexpr auto CMD_CONFIG = "cmd.config";

// ── video ──
inline constexpr auto VIDEO_ENCODE   = "video.encode";
inline constexpr auto VIDEO_PROBE    = "video.probe";
inline constexpr auto VIDEO_INFO     = "video.info";
inline constexpr auto VIDEO_OUTPUT   = "video.output";
inline constexpr auto VIDEO_BATCH    = "video.batch";
inline constexpr auto VIDEO_PROGRESS = "video.progress";
inline constexpr auto VIDEO_STATE    = "video.state";
inline constexpr auto VIDEO_PROCESS  = "video.process";

// ── picture ──
inline constexpr auto PICTURE_PROCESS  = "picture.process";
inline constexpr auto PICTURE_COMPRESS = "picture.compress";

// ── pack ──
inline constexpr auto PACK_ZIP     = "pack.zip";
inline constexpr auto PACK_SERVICE = "pack.service";

// ── core ──
inline constexpr auto CORE_SCAN     = "core.scan";
inline constexpr auto CORE_JOB      = "core.job";
inline constexpr auto CORE_TASK     = "core.task";
inline constexpr auto CORE_PARALLEL = "core.parallel";

// ── infra ──
inline constexpr auto INFRA_TOOLCHAIN = "infra.toolchain";
inline constexpr auto INFRA_CRASH     = "infra.crash";
inline constexpr auto INFRA_SIGNAL    = "infra.signal";

// ── utils ──
inline constexpr auto UTILS_SUBPROCESS = "utils.subprocess";

// ── test ──
inline constexpr auto TEST_INFRA = "test.infra";

}  // namespace logtags
