#include "video/probe_cache.h"

#include "infra/env.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <boost/json.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <chrono>
#include <cstdlib>  // IWYU pragma: keep -- needed with MSVC STL; Linux libstdc++ pulls it transitively
#include <fstream>
#include <string_view>
#if defined(_WIN32)
  #include <process.h>
#else
  #include <unistd.h>
#endif

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_PROBE);

namespace fs = std::filesystem;

namespace probecache {
namespace {

int currentProcessId() {
#if defined(_WIN32)
  return _getpid();
#else
  return getpid();
#endif
}

auto defaultCacheFilePath() -> fs::path {
  if (auto const overridePath = processenv::readNonEmptyEnvVar("ENCRO_PROBE_CACHE")) {
    return fs::path{*overridePath};
  }
  return resolveCommonLogDir().parent_path() / "probe-cache.json";
}

auto parseEntry(boost::json::value const& v) -> std::optional<Entry> {
  if (!v.is_object()) { return std::nullopt; }
  auto const& obj = v.as_object();
  auto entry = Entry{};
  if (auto const* s = obj.if_contains("key"); s != nullptr && s->is_string()) {
    entry.key = s->as_string();
  } else {
    return std::nullopt;
  }
  if (auto const* s = obj.if_contains("metric"); s != nullptr && s->is_string()) {
    entry.metric = s->as_string();
  } else {
    return std::nullopt;
  }
  entry.chosenCq = obj.if_contains("cq") && obj.at("cq").is_int64()
    ? static_cast<int>(obj.at("cq").as_int64())
    : 0;
  entry.p5 =
    obj.if_contains("p5") && obj.at("p5").is_double() ? obj.at("p5").as_double() : 0.0;
  auto readUint = [](boost::json::value const& v) -> std::uint64_t {
    if (v.is_uint64()) { return v.as_uint64(); }
    if (v.is_int64()) { return static_cast<std::uint64_t>(v.as_int64()); }
    return 0;
  };
  entry.estimatedBytes = obj.if_contains("bytes") ? readUint(obj.at("bytes")) : 0;
  entry.unreachableFloor = obj.if_contains("floor") && obj.at("floor").is_bool()
    ? obj.at("floor").as_bool()
    : false;
  entry.updatedAtMs = obj.if_contains("ts") ? readUint(obj.at("ts")) : 0;
  return entry;
}

}  // namespace

auto probeCacheKey(
  fs::path const& inputPath,
  std::uintmax_t fileSize,
  std::uint64_t mtimeMs,
  std::string const& codec,
  std::optional<std::string> const& preset,
  std::optional<int> maxrateKbps,
  int minVmaf,
  std::string_view metric
) -> std::string {
  auto const presetValue = preset.value_or(std::string{});
  return boost::json::serialize(
    boost::json::array{
      inputPath.string(),
      static_cast<std::uint64_t>(fileSize),
      mtimeMs,
      codec,
      presetValue,
      maxrateKbps.value_or(0),
      minVmaf,
      std::string{metric},
    }
  );
}

std::uint64_t lastWriteTimeMs(fs::path const& path) {
  auto ec = std::error_code{};
  auto const mtime = fs::last_write_time(path, ec);
  if (ec) { return 0; }
  // file_clock's epoch differs per platform (1601 vs 1970) but is constant
  // locally, so time_since_epoch is comparable within one machine's cache and
  // avoids to_utc()/from_utc(), which libstdc++ headers do not provide.
  auto const sinceEpoch = std::chrono::time_point_cast<std::chrono::milliseconds>(mtime);
  return static_cast<std::uint64_t>(sinceEpoch.time_since_epoch().count());
}

auto load(fs::path const& filePath) -> std::vector<Entry> {
  auto const resolvedPath = filePath.empty() ? defaultCacheFilePath() : filePath;
  auto file = std::ifstream{resolvedPath};
  if (!file.is_open()) { return {}; }

  // Corrupt JSON is an operational failure handled via error_code; only
  // catastrophic allocation failures may throw (caught by main).
  auto ec = std::error_code{};
  auto const value = boost::json::parse(
    std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}},
    ec
  );
  if (ec) { return {}; }  // Corrupt cache: discard and re-probe; never block a run.
  if (!value.is_object()) { return {}; }
  auto const& obj = value.as_object();
  auto const version = obj.if_contains("version") && obj.at("version").is_int64()
    ? static_cast<int>(obj.at("version").as_int64())
    : -1;
  if (version != kSchemaVersion) { return {}; }
  auto const& entries = obj.at("entries");
  if (!entries.is_array()) { return {}; }

  auto result = std::vector<Entry>{};
  for (auto const& item: entries.as_array()) {
    if (auto const entry = parseEntry(item); entry.has_value()) {
      result.push_back(entry.value());
    }
  }
  return result;
}

void save(std::vector<Entry> const& updates, fs::path const& filePath) {
  auto const resolvedPath = filePath.empty() ? defaultCacheFilePath() : filePath;
  auto entries = load(resolvedPath);

  auto const nowMs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()
    )
                                 .count());
  for (auto const& update: updates) {
    auto it = std::ranges::find_if(entries, [&update](Entry const& e) {
      return e.key == update.key;
    });
    if (it != entries.end()) {
      *it = update;
      it->updatedAtMs = nowMs;
    } else {
      entries.push_back(update);
      entries.back().updatedAtMs = nowMs;
    }
  }

  // Oldest-first eviction beyond the cap.
  std::ranges::sort(entries, [](Entry const& a, Entry const& b) {
    return a.updatedAtMs > b.updatedAtMs;
  });
  if (entries.size() > kMaxEntries) { entries.resize(kMaxEntries); }

  // Best-effort single-writer semantics: the flush is the only writer per
  // process, but a unique temp name (pid suffix) keeps two concurrent encro
  // processes from clobbering each other's staging file.
  auto const tmpPath =
    resolvedPath.string() + "." + std::to_string(currentProcessId()) + ".tmp";
  {
    auto out = std::ofstream{tmpPath};
    if (!out) {
      LOG_WARN("Failed to open probe cache for write: {}", tmpPath);
      return;
    }
    auto entriesArray = boost::json::array{};
    for (auto const& entry: entries) {
      entriesArray.push_back(
        boost::json::object{
          {"key", entry.key},
          {"cq", entry.chosenCq},
          {"p5", entry.p5},
          {"bytes", static_cast<std::uint64_t>(entry.estimatedBytes)},
          {"metric", entry.metric},
          {"floor", entry.unreachableFloor},
          {"ts", entry.updatedAtMs},
        }
      );
    }
    out << boost::json::serialize(
      boost::json::object{
        {"version", kSchemaVersion},
        {"entries", std::move(entriesArray)}
      }
    );
  }

  auto ec = std::error_code{};
  fs::rename(tmpPath, resolvedPath, ec);
  if (ec) { LOG_WARN("Failed to write probe cache: {}", ec.message()); }
}

}  // namespace probecache
