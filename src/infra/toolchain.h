#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

namespace toolchain {

auto resolve(appctx::AppConfig const& config, appctx::ToolchainPaths& out)
  -> eh::Result<void>;

}  // namespace toolchain
