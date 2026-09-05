// Content-hash cache of per-image analysis results: re-runs and interrupted
// runs never re-pay for an analyzed image (design D6, spec "Cache and
// resume"). The cache stores raw analysis only — never folder assignments.
#pragma once

#include "organize/organize_types.h"

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace organize {

// Confidence floor applied to stored pairs; bounds each cache entry without
// baking routing thresholds in. Routing thresholds must stay >= this floor to
// apply to cached images (design D6).
inline constexpr auto kCacheConfidenceFloor = 0.1;

class AnalysisCache {
public:
  // `filePath` is typically <root>/organized/.cache/analysis.json.
  explicit AnalysisCache(fs::path filePath);

  // Tolerant load: a missing, corrupt, or wrong-version file starts empty.
  void load();

  auto get(std::string const& contentHash) const -> std::optional<AnalysisResult>;

  // Thread-safe; persists after every mutation (atomic temp+rename write).
  // Pairs below kCacheConfidenceFloor are dropped before storing.
  void put(std::string const& contentHash, AnalysisResult const& result);

  // Discards the on-disk cache (--recluster escape hatch).
  void clear();

private:
  void saveLocked();

  fs::path filePath_;
  mutable std::mutex mutex_;
  std::map<std::string, AnalysisResult> entries_;
};

}  // namespace organize
