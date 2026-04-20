#pragma once

#include <boost/json.hpp>

#include <immer/atom.hpp>
#include <immer/map.hpp>

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

  using EncodingStateMap = immer::map<fs::path, std::shared_ptr<EncodingVideoState>>;

  struct EncodingStateStore {
    void set(std::shared_ptr<EncodingVideoState> const& state) {
      snapshot.update([state](EncodingStateMap const& states) {
        return states.set(state->inputPath, state);
      });
    }

    void erase(fs::path const& path) {
      snapshot.update([path](EncodingStateMap const& states) {
        return states.erase(path);
      });
    }

    auto find(fs::path const& path) const -> std::shared_ptr<EncodingVideoState> {
      auto const states = snapshot.load();
      if (auto const* state = states->find(path); state != nullptr) { return *state; }
      return {};
    }

    auto values() const -> std::vector<std::shared_ptr<EncodingVideoState>> {
      auto const states = snapshot.load();
      auto values = std::vector<std::shared_ptr<EncodingVideoState>>{};
      values.reserve(states->size());
      for (auto const& entry: *states) { values.emplace_back(entry.second); }
      return values;
    }

    auto size() const -> std::size_t { return snapshot.load()->size(); }

    auto load() const { return snapshot.load(); }

  private:
    immer::atom<EncodingStateMap> snapshot;
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
