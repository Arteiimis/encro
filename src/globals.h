#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include <boost/json.hpp>

struct Globals {
  template<class Ty>
  using path_map = std::unordered_map<std::filesystem::path, Ty>;

  // Add any global configurations or constants here if needed
  bool                                 YES_TO_ALL;
  bool                                 RECURSIVE;
  std::filesystem::path                INPUT_PATH;
  std::optional<std::filesystem::path> FFMPEG_INSTALL_DIR;
  std::optional<std::filesystem::path> FFMPEG_PATH;
  std::optional<std::filesystem::path> FFPROBE_PATH;
  std::optional<std::filesystem::path> OUTPUT_PATH;
  path_map<boost::json::value>         VIDEO_INFO_CACHE;
  path_map<std::filesystem::path>      PROGRESS_FILES;

  static Globals& instance() {
    static Globals instance;
    return instance;
  }
};

inline static auto& GLBs = Globals::instance();
