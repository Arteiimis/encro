#pragma once

#include "core/error_handle.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackPlan {
  std::vector<std::vector<fs::path>> groups;
  fs::path outputDir;
  std::function<std::string(std::size_t)> zipNameForIndex;
  std::function<std::string(std::size_t)> progressLabelForIndex;
  bool removeOnFailure = false;
};

auto packGroupsParallel(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;

}  // namespace pack
