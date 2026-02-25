#pragma once

#include "core/globals.h"

#include <filesystem>
#include <optional>
#include <string>

namespace appctx {

struct AppConfig {
  bool yesToAll = false;
  bool recursive = false;
  bool packOutput = false;
  bool packOnly = false;
  std::string processType = "video";
  std::string outputFormat = "mp4";
  std::filesystem::path inputPath;
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> ffmpegInstallDir;
};

struct ToolchainPaths {
  std::optional<std::filesystem::path> ffmpegPath;
  std::optional<std::filesystem::path> ffprobePath;
};

struct RuntimeContext {
  Globals::path_map<json::value> videoInfoCache;
  Globals::path_map<std::filesystem::path> progressFiles;
};

inline auto toGlobals(AppConfig const& config, Globals& globals) -> void {
  globals.YES_TO_ALL = config.yesToAll;
  globals.RECURSIVE = config.recursive;
  globals.PACK_OUTPUT = config.packOutput;
  globals.PACK_ONLY = config.packOnly;
  globals.PROCESS_TYPE = config.processType;
  globals.OUTPUT_FORMAT = config.outputFormat;
  globals.INPUT_PATH = config.inputPath;
  globals.OUTPUT_PATH = config.outputPath;
  globals.FFMPEG_INSTALL_DIR = config.ffmpegInstallDir;
}

}  // namespace appctx
