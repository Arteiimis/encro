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
#include <chrono>
#include <format>
#include <mutex>
#include <map>
#include <optional>
#include <set>

namespace organize {

namespace {

auto cachePathFor(Options const& options) -> fs::path {
  return options.root / "organized" / ".cache" / "analysis.json";
}

// Assigns an item to the folder for `tag`, honoring folder ownership
// (a teaching folder that claims the tag keeps images under its own name).
auto fileByCharacter(
  std::string const& tag,
  ImageItem& item,
  std::vector<FolderReference> const& references
) -> void {
  if (auto const* owner = owningFolder(references, tag); owner != nullptr) {
    item.folderName = owner->name;
    item.folderSource = FolderSource::FolderMatch;
    return;
  }
  auto sanitized = sanitizeCharacterName(tag);
  if (sanitized.empty()) { sanitized = fallbackCharacterName(tag); }
  item.folderName = fs::path{sanitized};
  item.folderSource = FolderSource::CharacterTag;
}

// Fixed routing order (spec): one confident tag -> character folder
// (ownership-redirected); competing identities -> mixed/; else clustering
// pending. A lone weak candidate (above the zero-evidence floor, below the
// strong threshold) still claims the image — a second subject with no
// identity signal does not make the image ownerless.
auto routeConfident(
  ImageItem& item,
  std::size_t index,
  double minConfidence,
  std::vector<FolderReference> const& references,
  std::map<std::string, std::size_t> const& characterDf,
  std::vector<std::size_t>& pending
) -> void {
  if (!item.analysis.has_value()) {
    pending.push_back(index);
    return;
  }
  auto const candidates = positiveCharacterTags(*item.analysis);
  auto const credible = std::vector<TagScore>{
    candidates.begin(),
    std::ranges::find_if_not(candidates, [&](TagScore const& tag) {
      return isCredibleCandidate(tag, characterDf);
    })
  };
  auto const strongCount =
    static_cast<std::size_t>(std::ranges::count_if(candidates, [](TagScore const& tag) {
      return tag.confidence >= kCharacterConfidence;
    }));
  if (strongCount == 1) {
    fileByCharacter(candidates.front().tag, item, references);
    return;
  }
  if (strongCount >= 2) {
    item.folderName = fs::path{kMixedFolder};
    item.folderSource = FolderSource::Mixed;
    return;
  }
  if (credible.size() == 1) {
    fileByCharacter(credible.front().tag, item, references);
    return;
  }
  if (credible.size() >= 2) {
    item.folderName = fs::path{kMixedFolder};
    item.folderSource = FolderSource::Mixed;
    return;
  }
  if (hasStrongCountTag(*item.analysis)) {
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
  auto const startedAt = std::chrono::steady_clock::now();
  for (auto& task: analysisTasks) {
    task.run = [run = std::move(task.run),
                &done,
                total,
                barIndex,
                progress,
                startedAt](taskexec::TaskContext& ctx) -> eh::Result<void> {
      auto outcome = run(ctx);
      auto const finished = done.fetch_add(1) + 1;
      if (progress != nullptr) {
        auto const percent =
          static_cast<float>(finished) / static_cast<float>(total) * 100.0F;
        auto const seconds =
          std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt)
            .count();
        auto const rate = seconds > 0.0F ? static_cast<float>(finished) / seconds : 0.0F;
        progress->setProgress(barIndex, percent);
        progress->setPostfixText(
          barIndex,
          std::format("{}/{} - {:.0f} img/s", finished, total, rate)
        );
      }
      return outcome;
    };
  }
  auto const runResult = taskexec::runTasks({
    .tasks = std::move(analysisTasks),
    .maxConcurrency = options.maxJobs,
    .progress = progress,
    // Per-image bar redraws flicker unless the cursor stays hidden for the
    // whole run (same as the encode/pack/picture pipelines).
    .hideCursor = true,
  });
  return !runResult.canceled;
}

// Routes every analyzed item; returns the single-subject remainder indices.
auto routeItems(
  std::vector<ImageItem>& items,
  double minConfidence,
  std::vector<FolderReference> const& references,
  std::map<std::string, std::size_t> const& characterDf
) -> std::vector<std::size_t> {
  auto pending = std::vector<std::size_t>{};
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    auto& item = items[index];
    if (!item.analysis.has_value()) {
      item.folderName = fs::path{kUncategorizedFolder};
      item.folderSource = FolderSource::Uncategorized;
      continue;
    }
    routeConfident(item, index, minConfidence, references, characterDf, pending);
  }
  return pending;
}

// Clusters the single-subject remainder; teaching folders claim clusters
// first, fresh clusters get unknown_ names.
// Fallback identity for images the clusterer could not group: files from
// the same download share a "<work>_<index>" stem prefix, and pages of one
// work almost always share its character cast.
auto sourceWorkKey(fs::path const& path) -> std::string {
  auto const stem = path.stem().string();
  auto const split = stem.find_last_of('_');
  if (split == std::string::npos) { return {}; }
  auto prefix = stem.substr(0, split);
  auto index = stem.substr(split + 1);
  if (
    prefix.empty()
    || index.empty()
    || !std::ranges::all_of(index, [](char c) { return c >= '0' && c <= '9'; })
    || !std::ranges::all_of(prefix, [](char c) { return c >= '0' && c <= '9'; })
  ) {
    return {};
  }
  return prefix;
}

// Folder-match: a cluster whose appearance resembles an existing folder
// joins it (teaching).
auto assignFolderMatches(
  std::vector<ImageItem>& items,
  std::vector<Cluster> const& clusters,
  std::vector<FolderReference> const& references
) -> void {
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
    if (matched == nullptr) { continue; }
    for (auto const index: cluster.itemIndices) {
      items[index].folderName = matched->name;
      items[index].folderSource = FolderSource::FolderMatch;
    }
  }
}

// Groups singleton clusters by their download-work stem prefix; pages of one
// work share its character cast. Returns one folder name per work group of
// two or more pages.
auto groupSingletonsByWork(
  std::vector<ImageItem>& items,
  std::vector<Cluster> const& clusters,
  std::set<std::string>& usedNames
) -> std::map<std::string, std::string> {
  auto workGroups = std::map<std::string, std::vector<std::size_t>>{};
  for (auto const& cluster: clusters) {
    if (cluster.itemIndices.size() != 1) { continue; }
    auto const index = cluster.itemIndices.front();
    if (!items[index].folderName.empty()) { continue; }
    auto const work = sourceWorkKey(items[index].path);
    if (!work.empty()) { workGroups[work].push_back(index); }
  }
  auto workNames = std::map<std::string, std::string>{};
  for (auto const& [work, indices]: workGroups) {
    if (indices.size() < 2) { continue; }  // a lone file is no group
    workNames[work] =
      assignUniqueFolderName(kUnknownPrefix + std::string{"source_"} + work, usedNames)
        .name;
    for (auto const index: indices) {
      items[index].folderName = fs::path{workNames[work]};
      items[index].folderSource = FolderSource::NewCluster;
    }
  }
  return workNames;
}

auto clusterRemainder(
  std::vector<ImageItem>& items,
  std::vector<std::size_t> const& pending,
  double minConfidence,
  std::vector<FolderReference> const& references,
  CorpusTraits const& traits
) -> void {
  auto const clusters = clusterPending(items, pending, traits);
  auto usedNames = std::set<std::string>{};
  for (auto const& item: items) {
    if (!item.folderName.empty()) {
      usedNames.insert(displaytext::pathToUtf8String(item.folderName));
    }
  }

  // Folder-match: a cluster whose appearance resembles an existing folder
  // joins it (teaching).
  assignFolderMatches(items, clusters, references);

  // Singleton clusters group by their download-work stem prefix (pages of
  // one work share its character cast); the group name is allocated once
  // per work, not per page.
  auto const workNames = groupSingletonsByWork(items, clusters, usedNames);

  // One name per cluster, allocated on its first unnamed member (allocation
  // consumes `usedNames`, so a second call for the same cluster would
  // collide-suffix every remaining member into its own folder).
  for (auto const& cluster: clusters) {
    auto clusterName = std::optional<fs::path>{};
    for (auto const index: cluster.itemIndices) {
      auto& item = items[index];
      if (!item.folderName.empty()) { continue; }
      if (
        auto const nameIt = workNames.find(sourceWorkKey(item.path));
        nameIt != workNames.end()
      ) {
        item.folderName = fs::path{nameIt->second};
        item.folderSource = FolderSource::NewCluster;
        continue;
      }
      if (!clusterName.has_value()) {
        clusterName = fs::path{clusterFolderName(cluster, items, usedNames)};
      }
      item.folderName = *clusterName;
      item.folderSource = FolderSource::NewCluster;
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

  auto cache = AnalysisCache{cachePathFor(options), kCacheFlushEveryPuts};
  if (options.recluster) {
    cache.clear();
  } else {
    cache.load();
  }
  auto const cacheHits = applyCachedAnalyses(items, cache);

  auto const analyzed = analyzeMissing(items, engine, cache, options, progress);
  // Persist the tail batch on every exit so an interrupted run loses at most
  // the in-flight images, never the completed-and-buffered ones.
  cache.flush();
  if (!analyzed) { return eh::makeError("interrupted: completed analysis is cached"); }

  // References rebuilt with the freshly cached analyses included, then the
  // fixed routing order per item. Idf weights come from the full analyzed
  // corpus so collection-constant tags cannot dominate similarity.
  auto const traits = buildCorpusTraits(items);
  auto const characterDf = buildCharacterDf(items);
  auto const references =
    buildFolderReferences(options.root, cache, options.minConfidence, traits);
  auto const pending = routeItems(items, options.minConfidence, references, characterDf);
  clusterRemainder(items, pending, options.minConfidence, references, traits);

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
