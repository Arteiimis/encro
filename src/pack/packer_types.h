#pragma once

#include "pack/pack_types.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pack::detail {

using ZipEntryNameResolver = std::function<std::string(std::filesystem::path const&)>;
using PackEntryProgressCallback = std::function<void(std::size_t, std::size_t)>;

struct PackGroupInput {
  std::filesystem::path filePath;
  std::filesystem::path sourceDir;
};

struct PackGroupPartition {
  std::vector<std::filesystem::path> filePaths;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

using PackEntryInput = pack::PackEntryInput;

struct PackEntryPartition {
  std::vector<pack::PackFileEntry> entries;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

}  // namespace pack::detail
