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

// Sanity/rating storage floor applied to stored pairs; general and character
// floors are stricter and derived from their consuming thresholds in
// save-land (cache.cpp). Routing thresholds must stay >= these floors to
// apply to cached images (design D6).
inline constexpr auto kCacheConfidenceFloor = 0.1;

// Put-batch size the organize pipeline uses. One atomic rewrite per analyzed
// image made cache I/O quadratic in collection size (an ~80MB store
// rewritten per image); batching bounds the rewrite count to one per N new
// analyses, at the cost of losing at most N-1 buffered analyses on a hard
// kill. flush() at stage boundaries and cancel paths covers every orderly
// exit.
inline constexpr std::size_t kCacheFlushEveryPuts = 64;

class AnalysisCache {
public:
  // `filePath` is typically <root>/organized/.cache/analysis.json.
  // flushEveryPuts = 1 persists on every put; larger values buffer puts and
  // rewrite the store only on every Nth put or on flush().
  explicit AnalysisCache(fs::path filePath, std::size_t flushEveryPuts = 1);

  // Tolerant load: a missing, corrupt, or wrong-version file starts empty.
  void load();

  auto get(std::string const& contentHash) const -> std::optional<AnalysisResult>;

  // Thread-safe; persists every flushEveryPuts-th mutation or on flush()
  // (atomic temp+rename write). Pairs below their category's storage floor
  // are dropped before storing.
  void put(std::string const& contentHash, AnalysisResult const& result);

  // Persists buffered puts (no-op when everything is already on disk).
  void flush();

  // Discards the on-disk cache (--recluster escape hatch).
  void clear();

private:
  void saveLocked();

  fs::path filePath_;
  mutable std::mutex mutex_;
  std::map<std::string, AnalysisResult> entries_;
  std::size_t flushEveryPuts_ = 1;
  std::size_t pendingPuts_ = 0;
};

}  // namespace organize
