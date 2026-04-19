#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

namespace pipeline {

auto run(appctx::AppContext& ctx) -> eh::Result<int>;

}  // namespace pipeline
