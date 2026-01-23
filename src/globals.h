#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include <boost/json.hpp>

struct Globals {
  // Add any global configurations or constants here if needed
  bool                                                          YES_TO_ALL;
  bool                                                          RECURSIVE;
  std::filesystem::path                                         INPUT_PATH;
  std::optional<std::filesystem::path>                          FFMPEG_INSTALL_DIR;
  std::optional<std::filesystem::path>                          FFMPEG_PATH;
  std::optional<std::filesystem::path>                          FFPROBE_PATH;
  std::optional<std::filesystem::path>                          OUTPUT_PATH;
  std::unordered_map<std::filesystem::path, boost::json::value> VIDEO_INFO_CACHE;
  std::unordered_map<std::filesystem::path, std::filesystem::path> PROGRESS_FILES;

  static Globals& instance() {
    static Globals instance;
    return instance;
  }
};

inline static auto& GLBs = Globals::instance();
