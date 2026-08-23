#pragma once

#include "core/collision_naming.h"
#include "core/work_dirs.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace videoseg {

namespace fs = std::filesystem;

// `<workRoot>\.encro\segments\<hash>\` — resume data for one task.
inline auto segmentDirForTask(fs::path const& workRoot, std::string_view taskId)
  -> fs::path {
  return workdirs::segmentsDir(
    workRoot,
    collisionnaming::shortPathHash(std::string{taskId})
  );
}

inline void createSegmentDir(fs::path const& dir) {
  auto ec = std::error_code{};
  fs::create_directories(dir, ec);
  workdirs::setHiddenOnEncroDir(dir);
}

inline void removeSegmentDir(fs::path const& dir) {
  auto ec = std::error_code{};
  fs::remove_all(dir, ec);
}

}  // namespace videoseg
