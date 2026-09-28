#include "organize/pipeline.h"

#include "core/display_text.h"
#include "core/media_item.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "organize/assign.h"
#include "organize/cache.h"
#include "organize/cluster.h"
#include "organize/execute.h"
#include "organize/naming.h"
#include "organize/scan.h"
#include "organize/teach.h"

#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>

namespace organize {

namespace {

auto cachePathFor(Options const& options) -> fs::path {
  return options.root / "organized" / ".cache" / "analysis.json";
}

// Assigns an item to the folder for `tag`, honoring folder ownership
// (a teaching folder that claims the tag keeps images under its own name).
void fileByCharacter(
  std::string const& tag,
  ImageItem& item,
  std::vector<FolderReference> const& references
) {
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
void routeConfident(
  ImageItem& item,
  std::size_t index,
  double minConfidence,
  std::vector<FolderReference> const& references,
  std::map<std::string, std::size_t> const& characterDf,
  std::vector<std::size_t>& pending
) {
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
std::size_t applyCachedAnalyses(std::vector<ImageItem>& items, AnalysisCache& cache) {
  auto cacheHits = std::size_t{0};
  for (auto index = std::size_t{0}; index < items.size(); ++index) {
    auto const cached = cache.get(items[index].contentHash);
    if (!cached.has_value()) { continue; }
    ++cacheHits;
    items[index].analysis = *cached;
  }
  return cacheHits;
}

// The analysis stage's skip filter, applied to analysis and to the bar's
// existence alike: applyCachedAnalyses filled `analysis` from the cache
// before the stage runs, so this is content-hash dedupe, not path progress.
bool alreadyAnalyzed(ImageItem const& item) {
  return item.analysis.has_value();
}

// Analyzes everything the cache does not cover; identical content dedupes
// through the cache, so the twin of an analyzed file is filtered instead of
// classified again. Failures cache an empty result so re-runs never re-pay for
// a deterministic decode failure; the two engines are independent, so a
// failure of one keeps the other's product. Returns false on cancel.
bool analyzeMissing(
  std::vector<ImageItem>& items,
  tagger::TaggerEngine& engine,
  tagger::FeatureEngine& features,
  AnalysisCache& cache,
  Options const& options,
  progress::ProgressContext* progress
) {
  auto cacheMutex = std::mutex{};
  // The bar is the flow's: created with today's prompt only when something is
  // pending, and left in place for the caller to erase.
  auto barIndex = std::size_t{0};
  if (progress != nullptr && std::ranges::any_of(items, [](ImageItem const& item) {
        return !alreadyAnalyzed(item);
      })) {
    barIndex = progress->addBar("Analyzing");
    progress->resetEta(barIndex);
  }

  auto const stageResult = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = progress,
      .barIndex = barIndex,
      .maxConcurrency = options.maxJobs,
      // The bar shows the rate instead of "{verb}: {done}/{total}"; `total` is
      // the stage's own count, i.e. the uncached remainder.
      .postfix =
        [](std::size_t done, std::size_t total, double elapsedSeconds) {
          auto const seconds = static_cast<float>(elapsedSeconds);
          auto const rate = seconds > 0.0F ? static_cast<float>(done) / seconds : 0.0F;
          return std::format("{}/{} - {:.0f} img/s", done, total, rate);
        },
    },
    std::span<ImageItem>{items},
    alreadyAnalyzed,
    [&engine, &features, &cache, &cacheMutex](ImageItem& item, taskexec::TaskContext&)
      -> eh::Result<void> {
      auto outcome = AnalysisResult{};
      if (auto result = engine.classify(item.path); result.has_value()) {
        outcome.tags = std::move(*result);
      }
      // Tags route and name, the feature clusters; either one alone is worth
      // caching, so a failure of one is not a failure of the image.
      if (auto identity = features.extract(item.path); identity.has_value()) {
        outcome.identity = std::move(*identity);
      }
      auto lock = std::lock_guard{cacheMutex};
      item.analysis = outcome;
      cache.put(item.contentHash, outcome);
      return {};
    }
  );
  return !stageResult.canceled;
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

// Folder-match: a cluster whose identity feature resembles an existing
// folder's mean joins it (teaching). Same threshold as clustering
// acceptance: one question, one answer (design D4).
void assignFolderMatches(
  std::vector<ImageItem>& items,
  std::vector<Cluster> const& clusters,
  std::vector<FolderReference> const& references,
  double combinedTau
) {
  for (auto const& cluster: clusters) {
    auto matched = static_cast<FolderReference const*>(nullptr);
    auto bestScore = 0.0;
    auto const clusterProfile =
      IdentityProfile{.feature = cluster.centroid, .tags = cluster.meanTags};
    for (auto const& reference: references) {
      if (reference.meanFeature.empty()) { continue; }
      auto const score = scoreProfiles(
        clusterProfile,
        IdentityProfile{.feature = reference.meanFeature, .tags = reference.meanTags}
      );
      // First best wins a tie: references are built in folder-name order, so
      // equal scores cannot pick a folder at random.
      if (score.value < tauFor(score, combinedTau)) { continue; }
      if (matched != nullptr && score.value <= bestScore) { continue; }
      matched = &reference;
      bestScore = score.value;
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

void clusterRemainder(
  std::vector<ImageItem>& items,
  std::vector<std::size_t> const& pending,
  std::vector<FolderReference> const& references,
  Options const& options
) {
  auto const result =
    clusterPending(items, pending, options.identityTau, options.clusterImageCeiling);
  auto const& clusters = result.clusters;
  if (result.fellBack) {
    // No silent degradation (design D5): the low-memory path names itself.
    terminal::messageln(
      terminal::MessageKind::Warning,
      "identity clustering: more than {} analysable images, falling back to the "
      "greedy pass",
      options.clusterImageCeiling
    );
  }
  auto usedNames = std::set<std::string>{};
  for (auto const& item: items) {
    if (!item.folderName.empty()) {
      usedNames.insert(displaytext::pathToUtf8String(item.folderName));
    }
  }

  // Folder-match: a cluster whose profile resembles an existing folder's joins
  // it (teaching).
  assignFolderMatches(items, clusters, references, options.identityTau);

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
  tagger::FeatureEngine& features,
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

  auto const analyzed = analyzeMissing(items, engine, features, cache, options, progress);
  // Persist the tail batch on every exit so an interrupted run loses at most
  // the in-flight images, never the completed-and-buffered ones.
  cache.flush();
  if (!analyzed) {
    // analyzeMissing reports false only when a stop canceled the task
    // executor, so this is the analysis phase's abort rather than a failure:
    // the command prints the cancellation notice, never the report.
    return ReportData{.canceled = true};
  }

  // References rebuilt with the freshly cached analyses included, then the
  // fixed routing order per item.
  auto const characterDf = buildCharacterDf(items);
  auto const references = buildFolderReferences(options.root, cache);
  auto const pending = routeItems(items, options.minConfidence, references, characterDf);
  clusterRemainder(items, pending, references, options);

  auto const stats = executeOrganize(options.root, items, options.dryRun);
  if (stats.canceled) { return ReportData{.canceled = true}; }
  return ReportData{
    .folders = buildFoldersSection(items),
    .scanned = items.size(),
    .copied = stats.copied,
    .skippedExisting = stats.skippedExisting,
    .cacheHits = cacheHits,
    .copyErrors = stats.errors,
  };
}

}  // namespace organize
