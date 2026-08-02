#pragma once

#include "cmd/cmd.h"
#include "core/app_context.h"

namespace prelude {

struct StartupContext {
  CmdParseResult cmd;
};

auto initStartup(int argc, char* argv[], std::string const& introLine) -> StartupContext;

void logConfigSummary(appctx::AppConfig const& config);

}  // namespace prelude
