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
  PackProgressCallbacks progressCallbacks{};
  // Test seam: invoked on the packing thread once per archive after the
  // finalizing counter goes up and before the archive trailer is written, so a
  // test can hold the finalizing window open. Production leaves it null.
  std::function<void()> onBeforeArchiveClose;
  std::optional<std::size_t> maxParallelJobs;
  bool removeOnFailure = false;
  bool compact = true;
};

auto execute(PackPlan const& plan, jobstate::Store* jobState = nullptr)
  -> eh::Result<PackRunResult>;

}  // namespace pack
