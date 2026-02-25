#pragma once

#include "core/app_context.h"
#include "core/error_handle.h"

#include <boost/program_options/variables_map.hpp>

namespace cmd {

auto buildConfig(boost::program_options::variables_map const& vm)
  -> eh::Result<appctx::AppConfig>;

}  // namespace cmd
