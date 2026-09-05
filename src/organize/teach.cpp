#include "organize/teach.h"

#include "organize/cluster.h"
#include "organize/sha256.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace organize {

namespace {

auto l2Norm(std::map<std::string, double> const& vector) -> double {
  auto sum = 0.0;
  for (auto const& [_, value]: vector) { sum += value * value; }
  return std::sqrt(sum);
}

}  // namespace

auto buildFolderReferences(
  fs::path const& root,
  AnalysisCache const& cache,
  double minConfidence
) -> std::vector<FolderReference> {
  auto const outputRoot = root / "organized";
  auto ec = std::error_code{};
  if (!fs::exists(outputRoot, ec) || ec) { return {}; }

  auto references = std::vector<FolderReference>{};
  for (auto const& entry: fs::directory_iterator{outputRoot, ec}) {
    if (!entry.is_directory()) { continue; }
    // Skip cache-internal directories; only character/unknown/mixed folders
    // teach.
    if (entry.path().filename() == ".cache") { continue; }
    auto reference = FolderReference{.name = entry.path().filename().string()};

    for (auto const& member: fs::directory_iterator{entry.path(), ec}) {
      if (!member.is_regular_file()) { continue; }
      auto file = std::ifstream{member.path(), std::ios::binary};
      if (!file.is_open()) { continue; }
      auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
      auto const cached = cache.get(sha256Hex(bytes));
      if (!cached.has_value()) { continue; }
      // Cached-but-empty analysis still counts as analyzable: routing
      // happened, it simply produced nothing.
      ++reference.analyzableMembers;

      auto const candidates = confidentCharacterTags(*cached, minConfidence);
      if (candidates.size() == 1) { ++reference.soleTagCounts[candidates.front().tag]; }

      auto vector = appearanceVector(*cached, minConfidence);
      auto const norm = l2Norm(vector);
      if (norm == 0.0) { continue; }
      for (auto& [_, value]: vector) { value /= norm; }

      auto const count = static_cast<double>(reference.vectorMembers);
      for (auto& [_, value]: reference.meanVector) { value *= count; }
      for (auto const& [tag, value]: vector) { reference.meanVector[tag] += value; }
      reference.vectorMembers += 1;
      auto const total = static_cast<double>(reference.vectorMembers);
      for (auto& [_, value]: reference.meanVector) { value /= total; }
    }

    if (reference.analyzableMembers > 0) { references.push_back(std::move(reference)); }
  }
  return references;
}

}  // namespace organize
