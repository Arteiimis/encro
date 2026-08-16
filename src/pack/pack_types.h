#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace pack {

struct PackFileEntry {
  fs::path sourcePath;
  std::string zipEntryName;
  bool isSummary = false;

  auto operator==(PackFileEntry const&) const -> bool = default;
};

struct PackEntryInput {
  PackFileEntry entry;
  fs::path sourceDir;
  std::optional<std::string> sourceKey;
  std::optional<std::string> fileKey;
  bool isSummary = false;

  auto operator==(PackEntryInput const&) const -> bool = default;
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

inline constexpr auto kDefaultMaxArchiveGroupSize = std::uintmax_t{500} * 1024 * 1024;

}  // namespace pack
