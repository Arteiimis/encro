#pragma once

#include "pack/pack_types.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pack::detail {

struct PackGroupInput {
  std::filesystem::path filePath;
  std::filesystem::path sourceDir;
};

struct PackGroupPartition {
  std::vector<std::filesystem::path> filePaths;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

struct PackEntryInput {
  pack::PackFileEntry entry;
  std::filesystem::path sourceDir;
  std::optional<std::string> sourceKey;
  std::optional<std::string> fileKey;
};

struct PackEntryPartition {
  std::vector<pack::PackFileEntry> entries;
  std::size_t partIndex = 0;
  std::size_t subPartIndex = 0;
};

}  // namespace pack::detail
