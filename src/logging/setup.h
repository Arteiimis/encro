#pragma once

#include <filesystem>
#include <optional>

namespace logging {

struct LogConfig {
    bool verboseEnabled{false};
    bool verboseEchoEnabled{false};
    bool colorsEnabled{true};
    std::optional<std::filesystem::path> customLogDir;
};

// 初始化日志系统: 创建共享 sink、注册 24 个 named async_logger、设置 default_logger。
// 返回创建的日志文件路径 (std::nullopt 如果 logging 未启用)。
[[nodiscard]] auto setup(LogConfig const& config) -> std::optional<std::filesystem::path>;

// 销毁: flush + 关闭所有 logger
auto shutdown() -> void;

}  // namespace logging
