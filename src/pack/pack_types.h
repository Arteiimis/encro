#pragma once

#include "core/error_handle.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackRunResult {
  int exitCode = 0;
  std::vector<fs::path> zippedFiles;
};

struct PackFileEntry {
  fs::path sourcePath;
  std::string zipEntryName;

  auto operator==(PackFileEntry const&) const -> bool = default;
};

struct FileOrdinalRange {
  std::size_t first = 0;
  std::size_t last = 0;
  std::size_t count = 0;
};

struct PackProgressCallbacks {
  std::function<void(std::size_t)> onGroupStart;
  std::function<void(std::size_t, std::filesystem::path const&)> onGroupSuccess;
  std::function<void(std::size_t, std::string const&)> onGroupFailure;
  std::function<void(std::size_t, std::size_t)> onCompactProgress;
  std::function<void(std::string_view)> onCompactStatusText;
};

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

static_assert(
  std::is_aggregate_v<pack::PackPlan>,
  "PackPlan must remain an aggregate for designated-initializer usage"
);

inline constexpr auto kDefaultMaxArchiveGroupSize = std::uintmax_t{500 * 1024 * 1024};

}  // namespace pack
