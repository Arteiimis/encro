#pragma once

#define PACK_PLAN_INTERNAL_INCLUDED

#include "pack/pack.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackPlan {
  std::vector<std::vector<PackFileEntry>> groups;
  fs::path outputDir;
  std::function<std::string(std::size_t)> zipNameForIndex;
  std::function<std::string(std::size_t)> progressLabelForIndex;
  PackProgressCallbacks progressCallbacks{};
  std::optional<std::size_t> maxParallelJobs;
  bool removeOnFailure = false;
  bool compact = true;
};

auto execute(PackPlan const& plan, jobstate::Store* jobState = nullptr)
  -> eh::Result<PackRunResult>;

}  // namespace pack
