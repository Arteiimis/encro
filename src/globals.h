#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>

#include <boost/json.hpp>

namespace fs = std::filesystem;
namespace json = boost::json;

struct Globals {
  template<class Ty>
  using path_map = std::unordered_map<fs::path, Ty>;

  // Add any global configurations or constants here if needed
  bool YES_TO_ALL;
  bool RECURSIVE;
  std::string PROCESS_TYPE;
  std::string OUTPUT_FORMAT;
  fs::path INPUT_PATH;
  std::optional<fs::path> FFMPEG_INSTALL_DIR;
  std::optional<fs::path> FFMPEG_PATH;
  std::optional<fs::path> FFPROBE_PATH;
  std::optional<fs::path> OUTPUT_PATH;
  path_map<json::value> VIDEO_INFO_CACHE;
  path_map<fs::path> PROGRESS_FILES;

  static Globals& instance() {
    static Globals instance;
    return instance;
  }
};

inline static auto& GLBs = Globals::instance();
