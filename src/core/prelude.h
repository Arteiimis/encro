#pragma once

#include "cmd/cmd.h"
#include "core/app_context.h"

#include <filesystem>
#include <optional>

namespace prelude {

struct StartupContext {
  CmdParseResult cmd;
  std::optional<std::filesystem::path> verboseLogFilePath;
};

auto initStartup(int argc, char* argv[]) -> StartupContext;

void printVerboseLogDirHint(
  std::optional<std::filesystem::path> const& verboseLogFilePath
);

void logConfigSummary(appctx::AppConfig const& config);

}  // namespace prelude
