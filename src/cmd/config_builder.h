#pragma once

#include "cmd/cmd.h"
#include "core/app_context.h"
#include "core/error_handle.h"

namespace cmd {

auto buildConfig(CmdParseResult const& result) -> eh::Result<appctx::AppConfig>;

}  // namespace cmd
