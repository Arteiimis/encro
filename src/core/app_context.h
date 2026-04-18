#pragma once

#include <boost/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace jobstate {
class Store;
}

namespace appctx {

namespace fs = std::filesystem;
namespace json = boost::json;

template<class Ty>
using path_map = std::unordered_map<fs::path, Ty>;

enum class OutputLayout {
  Flat,
  Keep,
};

struct AppConfig {
  bool yesToAll = false;
  bool recursive = false;
  bool packOutput = false;
  bool packOnly = false;
  bool resumeState = false;
  bool restartState = false;
  bool forceNameConflictHandling = true;
  bool verbose = false;
  bool verboseEcho = false;
  std::optional<std::size_t> maxParallelJobs;
  OutputLayout outputLayout = OutputLayout::Flat;
  std::string processType = "video";
  std::string outputFormat = "mp4";
  fs::path inputPath;
  std::vector<fs::path> inputPaths;
  std::optional<fs::path> outputPath;
  std::optional<fs::path> stateFilePath;
  std::optional<fs::path> ffmpegInstallDir;
};

struct ToolchainPaths {
  std::optional<fs::path> ffmpegPath;
  std::optional<fs::path> ffprobePath;
};

struct RuntimeContext {
  path_map<json::value> videoInfoCache;

  struct EncodingVideoState {
    fs::path inputPath;
    std::optional<std::string> actionId;
    std::optional<fs::path> outputPath;
    std::optional<fs::path> plannedOutputFile;
    std::optional<fs::path> outputFile;
    std::optional<fs::path> progressFilePath;
    std::optional<std::size_t> barIndex;
    std::optional<std::chrono::steady_clock::time_point> startTime;
    std::optional<std::chrono::steady_clock::time_point> endTime;
    std::optional<float> lastProgress;
    std::optional<uint64_t> lastFrameCount;
    std::optional<std::string> lastStatus;
    std::optional<std::string> lastError;
    bool finished = false;
    bool success = false;
    std::mutex mtx;
  };

  struct EncodingStateStore {
    path_map<std::shared_ptr<EncodingVideoState>> map;
    std::mutex mtx;
  } encodingStates;

  std::shared_ptr<::jobstate::Store> jobState;
};

using EncodingState = RuntimeContext::EncodingVideoState;
using EncodingStatePtr = std::shared_ptr<EncodingState>;
using EncodingStateList = std::vector<EncodingStatePtr>;

struct AppContext {
  AppConfig config;
  ToolchainPaths toolchain;
  RuntimeContext runtime;
};

}  // namespace appctx
