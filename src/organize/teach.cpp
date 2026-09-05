#include "organize/teach.h"

#include "core/sha256.h"
#include "organize/cluster.h"

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

// Folds one folder member into the reference. Returns false when the member
// is not analyzable (missing from the cache).
auto accumulateMember(
  FolderReference& reference,
  AnalysisResult const& analysis,
  double minConfidence
) -> bool {
  // Cached-but-empty analysis still counts as analyzable: routing happened,
  // it simply produced nothing.
  ++reference.analyzableMembers;

  auto const candidates = confidentCharacterTags(analysis, minConfidence);
  if (candidates.size() == 1) { ++reference.soleTagCounts[candidates.front().tag]; }

  auto vector = appearanceVector(analysis, minConfidence);
  auto const norm = l2Norm(vector);
  if (norm == 0.0) { return true; }
  for (auto& [_, value]: vector) { value /= norm; }

  auto const count = static_cast<double>(reference.vectorMembers);
  for (auto& [_, value]: reference.meanVector) { value *= count; }
  for (auto const& [tag, value]: vector) { reference.meanVector[tag] += value; }
  reference.vectorMembers += 1;
  auto const total = static_cast<double>(reference.vectorMembers);
  for (auto& [_, value]: reference.meanVector) { value /= total; }
  return true;
}

auto buildReference(
  fs::path const& folderDir,
  AnalysisCache const& cache,
  double minConfidence
) -> FolderReference {
  auto reference = FolderReference{.name = folderDir.filename()};
  auto ec = std::error_code{};
  for (auto const& member: fs::directory_iterator{folderDir, ec}) {
    if (!member.is_regular_file()) { continue; }
    auto file = std::ifstream{member.path(), std::ios::binary};
    if (!file.is_open()) { continue; }
    auto const bytes = std::string{std::istreambuf_iterator<char>{file}, {}};
    auto const cached = cache.get(core::sha256Hex(bytes));
    if (!cached.has_value()) { continue; }
    accumulateMember(reference, *cached, minConfidence);
  }
  return reference;
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
    // Skip cache-internal directories; only character/unknown/mixed folders
    // teach.
    if (!entry.is_directory() || entry.path().filename() == ".cache") { continue; }
    auto reference = buildReference(entry.path(), cache, minConfidence);
    if (reference.analyzableMembers > 0) { references.push_back(std::move(reference)); }
  }
  return references;
}

}  // namespace organize
