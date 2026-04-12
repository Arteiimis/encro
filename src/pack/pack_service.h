#pragma once

#include "core/error_handle.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct FileOrdinalRange {
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t count = 0;
};

struct PackPlan {
  std::vector<std::vector<fs::path>> groups;
  fs::path outputDir;
  std::function<std::string(std::size_t)> zipNameForIndex;
  std::function<std::string(std::size_t)> progressLabelForIndex;
  std::function<std::string(fs::path const&)> zipEntryNameForFile;
  std::optional<std::size_t> maxParallelJobs;
  bool removeOnFailure = false;
};

auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange>;

auto appendOrdinalRangeSuffix(
  std::string_view fileName,
  FileOrdinalRange const& range
) -> std::string;

auto packGroupsParallel(PackPlan const& plan) -> eh::Result<std::vector<fs::path>>;

}  // namespace pack
