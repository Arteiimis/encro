#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace probecache {

// One persisted probe decision, keyed by the full decision inputs (D2/D2a in
// design.md). A hit restores the whole plan payload CQ/p5/size/metric/floor.
struct Entry {
  std::string key;  // probeCacheKey(...) fingerprint
  int chosenCq = 0;
  double p5 = 0.0;
  std::uintmax_t estimatedBytes = 0;
  std::string metric;  // "VMAF" | "SSIM"
  bool unreachableFloor = false;
  std::uint64_t updatedAtMs = 0;
};

constexpr auto kSchemaVersion = 1;
constexpr auto kMaxEntries = std::size_t{2000};

// Key = serialized JSON array of the decision inputs; preset/maxrate are the
// resolved values (post-resolveInputEncodeSettings), not raw config.
auto probeCacheKey(
  fs::path const& inputPath,
  std::uintmax_t fileSize,
  std::uint64_t mtimeMs,
  std::string const& codec,
  std::optional<std::string> const& preset,
  std::optional<int> maxrateKbps,
  int minVmaf,
  std::string_view metric
) -> std::string;

auto lastWriteTimeMs(fs::path const& path) -> std::uint64_t;

// Loads the cache file; a missing file yields an empty cache, a corrupt file
// or schema mismatch yields an empty cache (re-probe + rewrite). File lives
// next to the encro data directory (log-dir parent); an explicit filePath is
// for tests.
auto load(fs::path const& filePath = {}) -> std::vector<Entry>;

// Merges updates into the cache, drops oldest entries beyond the cap, and
// writes atomically (temp file + rename). Best-effort: a write failure is
// logged and ignored so probing never fails because of the cache.
auto save(std::vector<Entry> const& updates, fs::path const& filePath = {}) -> void;

}  // namespace probecache
