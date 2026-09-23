#pragma once

#include <boost/json.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
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
  bool videoWebp = false;
  std::optional<int> imageQuality;
  std::optional<int> crf;
  int minVmaf = 95;
  bool dryRun = false;
  std::optional<std::string> nvencPreset;
  std::optional<std::string> videoCodec;
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

struct RuntimeContext {
  struct VideoInfoCacheStore {
    void set(fs::path const& path, json::value const& value) {
      auto lock = std::scoped_lock{mtx_};  // NOLINT(bugprone-unused-raii)
      cache_[path] = value;
    }

    auto find(fs::path const& path) const -> std::optional<json::value> {
      auto lock = std::shared_lock{mtx_};  // NOLINT(bugprone-unused-raii)
      if (auto const it = cache_.find(path); it != cache_.end()) { return it->second; }
      return std::nullopt;
    }

    std::size_t size() const {
      auto lock = std::shared_lock{mtx_};  // NOLINT(bugprone-unused-raii)
      return cache_.size();
    }

  private:
    mutable std::shared_mutex mtx_;
    path_map<json::value> cache_;
  } videoInfoCache;

  std::shared_ptr<::jobstate::Store> jobState;
  bool jobStateMatched = false;
};

struct AppContext {
  AppConfig config;
  ToolchainPaths toolchain;
  RuntimeContext runtime;
};

}  // namespace appctx
