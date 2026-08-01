#pragma once

#include "core/collision_naming.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace videoseg {

namespace fs = std::filesystem;

inline auto segmentDirForTask(std::string_view taskId) -> fs::path {
  return fs::temp_directory_path()
    / "encro"
    / std::format("segments_{}", collisionnaming::shortPathHash(std::string{taskId}));
}

inline auto createSegmentDir(fs::path const& dir) -> void {
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);
}

inline auto removeSegmentDir(fs::path const& dir) -> void {
  auto ec = std::error_code{};
  fs::remove_all(dir, ec);
}

}  // namespace videoseg
