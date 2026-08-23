#pragma once

#include "core/app_context.h"

#include <functional>

using function_ref = std::function<void(std::string const&)> const&;

bool encodeVideo(
  appctx::AppContext& ctx,
  appctx::EncodingState& state,
  function_ref statusUpdater = {},
  std::size_t workerCount = 1
);
