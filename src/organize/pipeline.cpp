#include "organize/pipeline.h"

#include "core/display_text.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "organize/assign.h"
#include "organize/cache.h"
#include "organize/cluster.h"
#include "organize/execute.h"
#include "organize/naming.h"
#include "organize/scan.h"
#include "organize/teach.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <set>

namespace organize {

namespace {

auto cachePathFor(Options const& options) -> fs::path {
  return options.root / "organized" / ".cache" / "analysis.json";
}

// Fixed routing order (spec): one confident tag -> character folder
// (ownership-redirected); multi-subject -> mixed/; else clustering pending.
auto routeConfident(
  ImageItem& item,
  std::size_t index,
  double minConfidence,
  std::vector<FolderReference> const& references,
  std::vector<std::size_t>& pending
) -> void {
  if (!item.analysis.has_value()) {
    pending.push_back(index);
    return;
  }
  auto const confident = confidentCharacterTags(*item.analysis);
  if (confident.size() == 1) {
    auto const tag = confident.front().tag;
    if (auto const* owner = owningFolder(references, tag); owner != nullptr) {
      item.folderName = owner->name;
      item.folderSource = FolderSource::FolderMatch;
      return;
    }
    auto sanitized = sanitizeCharacterName(tag);
    if (sanitized.empty()) { sanitized = fallbackCharacterName(tag); }
    item.folderName = fs::path{sanitized};
    item.folderSource = FolderSource::CharacterTag;
    return;
  }
  if (confident.size() >= 2 || isMultiSubject(*item.analysis, minConfidence)) {
    item.folderName = fs::path{kMixedFolder};
    item.folderSource = FolderSource::Mixed;
    return;
  }
  pending.push_back(index);
}

// Loads cached analyses into the items; returns how many were covered.
auto applyCachedAnalyses(std::vector<ImageItem>& items, AnalysisCache& cache)
  -> std::size_t {
  auto cacheHits = std::size_t{0};
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    auto const cached = cache.get(items[index].contentHash);
    if (!cached.has_value()) { continue; }
    ++cacheHits;
    items[index].analysis = *cached;
  }
  return cacheHits;
}

// Analyzes everything the cache does not cover; identical content dedupes
// through the cache on the fly. Failures cache an empty result so re-runs
// never re-pay for a deterministic decode failure. Returns false on cancel.
auto analyzeMissing(
  std::vector<ImageItem>& items,
  tagger::TaggerEngine& engine,
  AnalysisCache& cache,
  Options const& options,
  progress::ProgressContext* progress
) -> bool {
  auto analysisTasks = std::vector<taskexec::TaskSpec>{};
  auto cacheMutex = std::mutex{};
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    if (cache.get(items[index].contentHash).has_value()) { continue; }
    analysisTasks.push_back(
      taskexec::TaskSpec{
        .id = items[index].contentHash,
        .label = items[index].path.filename().string(),
        .input = items[index].path.string(),
        .run = [&engine, &items, &cache, &cacheMutex, index](taskexec::TaskContext&)
          -> eh::Result<void> {
          auto result = engine.classify(items[index].path);
          auto outcome = AnalysisResult{};
          if (result.has_value()) { outcome = std::move(*result); }
          auto lock = std::lock_guard{cacheMutex};
          items[index].analysis = outcome;
          cache.put(items[index].contentHash, outcome);
          return {};
        },
      }
    );
  }
  if (analysisTasks.empty()) { return true; }

  auto barIndex = progress != nullptr ? progress->addBar("Analyzing")
                                      : std::numeric_limits<std::size_t>::max();
  if (progress != nullptr) { progress->resetEta(barIndex); }
  auto const total = analysisTasks.size();
  auto done = std::atomic<std::size_t>{0};
  for (auto& task: analysisTasks) {
    task.run = [run = std::move(task.run),
                &done,
                total,
                barIndex,
                progress](taskexec::TaskContext& ctx) -> eh::Result<void> {
      auto outcome = run(ctx);
      auto const finished = done.fetch_add(1) + 1;
      if (progress != nullptr) {
        auto const percent =
          static_cast<float>(finished) / static_cast<float>(total) * 100.0F;
        progress->setProgress(barIndex, percent);
        progress->setPostfixText(barIndex, std::format("{}/{}", finished, total));
      }
      return outcome;
    };
  }
  auto const runResult = taskexec::runTasks({
    .tasks = std::move(analysisTasks),
    .maxConcurrency = options.maxJobs,
    .progress = progress,
  });
  return !runResult.canceled;
}

// Routes every analyzed item; returns the single-subject remainder indices.
auto routeItems(
  std::vector<ImageItem>& items,
  double minConfidence,
  std::vector<FolderReference> const& references
) -> std::vector<std::size_t> {
  auto pending = std::vector<std::size_t>{};
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    auto& item = items[index];
    if (!item.analysis.has_value()) {
      item.folderName = fs::path{kUncategorizedFolder};
      item.folderSource = FolderSource::Uncategorized;
      continue;
    }
    routeConfident(item, index, minConfidence, references, pending);
  }
  return pending;
}

// Clusters the single-subject remainder; teaching folders claim clusters
// first, fresh clusters get unknown_ names.
auto clusterRemainder(
  std::vector<ImageItem>& items,
  std::vector<std::size_t> const& pending,
  double minConfidence,
  std::vector<FolderReference> const& references
) -> void {
  auto const clusters = clusterPending(items, pending, minConfidence);
  auto usedNames = std::set<std::string>{};
  for (auto const& item: items) {
    if (!item.folderName.empty()) {
      usedNames.insert(displaytext::pathToUtf8String(item.folderName));
    }
  }
  for (auto const& cluster: clusters) {
    auto matched = static_cast<FolderReference const*>(nullptr);
    auto bestScore = kFolderTau;
    for (auto const& reference: references) {
      if (reference.meanVector.empty()) { continue; }
      auto const score = cosineSimilarity(cluster.centroid, reference.meanVector);
      if (score > bestScore) {
        matched = &reference;
        bestScore = score;
      }
    }
    if (matched != nullptr) {
      for (auto const index: cluster.itemIndices) {
        items[index].folderName = matched->name;
        items[index].folderSource = FolderSource::FolderMatch;
      }
      continue;
    }
    auto const name = clusterFolderName(cluster, items, usedNames);
    for (auto const index: cluster.itemIndices) {
      items[index].folderName = name;
      items[index].folderSource = FolderSource::NewCluster;
    }
  }

  // Anything still unassigned (empty vectors never joined a cluster) lands
  // in uncategorized so every image lands in exactly one folder.
  for (auto& item: items) {
    if (item.folderName.empty()) {
      item.folderName = fs::path{kUncategorizedFolder};
      item.folderSource = FolderSource::Uncategorized;
    }
  }
}

}  // namespace

auto runOrganize(
  Options const& options,
  tagger::TaggerEngine& engine,
  progress::ProgressContext* progress
) -> eh::Result<ReportData> {
  auto scanned = scanImages(options.root, options.recursive);
  if (!scanned) { return std::unexpected(scanned.error()); }
  auto items = std::move(*scanned);

  auto cache = AnalysisCache{cachePathFor(options)};
  if (options.recluster) {
    cache.clear();
  } else {
    cache.load();
  }
  auto const cacheHits = applyCachedAnalyses(items, cache);

  if (!analyzeMissing(items, engine, cache, options, progress)) {
    return eh::makeError("interrupted: completed analysis is cached");
  }

  // References rebuilt with the freshly cached analyses included, then the
  // fixed routing order per item.
  auto const references =
    buildFolderReferences(options.root, cache, options.minConfidence);
  auto const pending = routeItems(items, options.minConfidence, references);
  clusterRemainder(items, pending, options.minConfidence, references);

  auto const stats = executeOrganize(options.root, items, options.dryRun);
  return ReportData{
    .folders = buildFoldersSection(items),
    .scanned = items.size(),
    .copied = stats.copied,
    .skippedExisting = stats.skippedExisting,
    .cacheHits = cacheHits,
  };
}

}  // namespace organize
