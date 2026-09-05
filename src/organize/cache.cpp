#include "organize/cache.h"

#include "organize/assign.h"
#include "organize/cluster.h"

#include <boost/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace organize {

namespace {

namespace json = boost::json;

constexpr auto kVersion = 1;

auto tagsToJson(std::vector<TagScore> const& tags) -> json::value {
  auto array = json::array{};
  for (auto const& tag: tags) {
    array.emplace_back(json::array{json::string{tag.tag}, tag.confidence});
  }
  return array;
}

auto tagsFromJson(json::value const& value) -> std::vector<TagScore> {
  auto tags = std::vector<TagScore>{};
  if (!value.is_array()) { return tags; }
  for (auto const& pair: value.as_array()) {
    if (!pair.is_array() || pair.as_array().size() != 2) { continue; }
    auto const& fields = pair.as_array();
    if (!fields[0].is_string() || !fields[1].is_number()) { continue; }
    tags.push_back(
      TagScore{
        .tag = std::string{fields[0].as_string().c_str()},
        .confidence = fields[1].to_number<double>(),
      }
    );
  }
  return tags;
}

auto resultToJson(AnalysisResult const& result) -> json::value {
  return json::object{
    {"general", tagsToJson(result.general)},
    {"character", tagsToJson(result.character)},
    {"rating", tagsToJson(result.rating)},
  };
}

auto resultFromJson(json::value const& value) -> AnalysisResult {
  auto result = AnalysisResult{};
  if (!value.is_object()) { return result; }
  auto const& object = value.as_object();
  if (auto const* general = object.if_contains("general")) {
    result.general = tagsFromJson(*general);
  }
  if (auto const* character = object.if_contains("character")) {
    result.character = tagsFromJson(*character);
  }
  if (auto const* rating = object.if_contains("rating")) {
    result.rating = tagsFromJson(*rating);
  }
  return result;
}

auto aboveFloor(TagScore const& tag, double floor) -> bool {
  return tag.confidence >= floor;
}

// Storage floors per category: the cache keeps exactly what a downstream
// stage can read, and nothing more. Appearance vectors and corpus traits
// consume general tags at kVectorFloor and up; character routing consumes
// kWeakConfidence and up — below that, the unused identity head emits
// ~0.50-0.52 noise for every vocabulary character, which used to dominate
// the store (~2.7k dead pairs per image) without ever influencing a
// decision. Derived from the consuming constants so a floor can never
// silently exceed a threshold that reads the cache (design D6).
auto keepAtOrAbove(std::vector<TagScore> const& tags, double floor)
  -> std::vector<TagScore> {
  auto kept = std::vector<TagScore>{};
  kept.reserve(tags.size());
  std::copy_if(
    tags.begin(),
    tags.end(),
    std::back_inserter(kept),
    [&](TagScore const& tag) { return aboveFloor(tag, floor); }
  );
  return kept;
}

}  // namespace

AnalysisCache::AnalysisCache(fs::path filePath, std::size_t flushEveryPuts)
  : filePath_(std::move(filePath)),
    flushEveryPuts_(std::max<std::size_t>(1, flushEveryPuts)) { }

void AnalysisCache::load() {
  auto lock = std::lock_guard{mutex_};
  entries_.clear();

  auto ec = std::error_code{};
  if (!fs::exists(filePath_, ec) || ec) { return; }

  auto file = std::ifstream{filePath_, std::ios::binary};
  if (!file.is_open()) { return; }

  auto const content = std::string{std::istreambuf_iterator<char>{file}, {}};
  auto parseEc = boost::system::error_code{};
  auto const parsed = json::parse(content, parseEc);
  if (parseEc || !parsed.is_object()) { return; }

  auto const& object = parsed.as_object();
  auto const* images = object.if_contains("images");
  if (images == nullptr || !images->is_object()) { return; }
  auto const* version = object.if_contains("version");
  if (
    version == nullptr || !version->is_number() || version->to_number<int>() != kVersion
  ) {
    return;
  }

  for (auto const& [hash, value]: images->as_object()) {
    entries_.insert_or_assign(std::string{hash}, resultFromJson(value));
  }
}

auto AnalysisCache::get(std::string const& contentHash) const
  -> std::optional<AnalysisResult> {
  auto lock = std::lock_guard{mutex_};
  auto const it = entries_.find(contentHash);
  return it == entries_.end() ? std::nullopt : std::optional{it->second};
}

void AnalysisCache::put(std::string const& contentHash, AnalysisResult const& result) {
  auto filtered = AnalysisResult{};
  filtered.general = keepAtOrAbove(result.general, kVectorFloor);
  filtered.character = keepAtOrAbove(result.character, kWeakConfidence);
  filtered.rating = keepAtOrAbove(result.rating, kCacheConfidenceFloor);

  auto lock = std::lock_guard{mutex_};
  entries_.insert_or_assign(contentHash, std::move(filtered));
  if (++pendingPuts_ < flushEveryPuts_) { return; }
  saveLocked();
  pendingPuts_ = 0;
}

void AnalysisCache::flush() {
  auto lock = std::lock_guard{mutex_};
  if (pendingPuts_ == 0) { return; }
  saveLocked();
  pendingPuts_ = 0;
}

void AnalysisCache::clear() {
  auto lock = std::lock_guard{mutex_};
  entries_.clear();
  pendingPuts_ = 0;
  auto ec = std::error_code{};
  fs::remove(filePath_, ec);
}

// Atomic-ish rewrite: serialize to a temp file in the same directory, then
// rename over the target so an interrupted write never corrupts the cache.
void AnalysisCache::saveLocked() {
  auto images = json::object{};
  for (auto const& [hash, entry]: entries_) {
    images[json::string{hash}] = resultToJson(entry);
  }
  auto const content =
    json::serialize(json::object{{"version", kVersion}, {"images", std::move(images)}});

  auto ec = std::error_code{};
  fs::create_directories(filePath_.parent_path(), ec);

  auto const tempPath = filePath_.string() + ".tmp";
  {
    auto out = std::ofstream{tempPath, std::ios::binary};
    if (!out.is_open()) { return; }
    out << content;
    out.flush();
    if (!out) { return; }
  }
  fs::rename(tempPath, filePath_, ec);
}

}  // namespace organize
