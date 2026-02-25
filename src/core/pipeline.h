#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <memory>

namespace pipeline {

class IPipeline {
public:
  virtual ~IPipeline() = default;
  virtual eh::Result<int> run(appctx::AppContext& ctx) = 0;
};

auto selectPipeline(appctx::AppContext& ctx)
  -> eh::Result<std::unique_ptr<IPipeline>>;

}  // namespace pipeline
