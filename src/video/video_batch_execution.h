#pragma once

#include "core/app_context.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <immer/map.hpp>

namespace fs = std::filesystem;

namespace videobatch {

using ActionIdMap = immer::map<fs::path, std::string>;
using EncodeResultsMap = immer::map<fs::path, bool>;

auto runEncodingTasks(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  ActionIdMap const& actionIds,
  std::size_t overallTotalCount,
  std::size_t initialCompletedCount
) -> std::optional<EncodeResultsMap>;

}  // namespace videobatch
