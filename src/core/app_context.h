#pragma once

#include <boost/json.hpp>

#include <immer/atom.hpp>
#include <immer/map.hpp>

#include <atomic>
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
  bool pictureFolderSummary = false;
  bool compressImages = false;
  std::optional<int> imageQuality;
  std::optional<int> crf;
  std::optional<std::string> nvencPreset;
  bool verbose = false;
  bool fullProgress = false;
  bool jsonEnabled = false;
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

struct EncodingState {
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
  std::atomic<float> lastProgressAtomic{-1.0f};
  std::optional<uint64_t> lastFrameCount;
  std::optional<std::string> lastStatus;
  std::optional<std::string> lastError;
  std::optional<int64_t> totalFrames;
  std::uint64_t baseFrameOffset = 0;
  std::optional<int> subprocessPid;
  std::optional<std::string> subprocessCmdline;
  bool finished = false;
  bool success = false;
  std::mutex mtx;
};

using EncodingStatePtr = std::shared_ptr<EncodingState>;
using EncodingStateList = std::vector<EncodingStatePtr>;

struct RuntimeContext {
  using VideoInfoCacheMap = immer::map<fs::path, json::value>;

  struct VideoInfoCacheStore {
    void set(fs::path const& path, json::value const& value) {
      snapshot.update([path, value](VideoInfoCacheMap const& cache) {
        return cache.set(path, value);
      });
    }

    auto find(fs::path const& path) const -> std::optional<json::value> {
      auto const cache = snapshot.load();
      if (auto const* value = cache->find(path); value != nullptr) { return *value; }
      return std::nullopt;
    }

    auto size() const -> std::size_t { return snapshot.load()->size(); }

    auto load() const { return snapshot.load(); }

  private:
    immer::atom<VideoInfoCacheMap> snapshot;
  } videoInfoCache;

  std::shared_ptr<::jobstate::Store> jobState;
};

struct AppContext {
  AppConfig config;
  ToolchainPaths toolchain;
  RuntimeContext runtime;
};

}  // namespace appctx
