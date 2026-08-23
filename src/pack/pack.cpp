// pack::execute() — Task 1 + Task 2: non-resumable + resumable paths
//
// Single entry point D-05. Internally manages Packer/PackService lifecycle,
// handles grouping + naming + PackPlan construction, and runs packing.
// Resumable execution (D-11) is fully internalized here.
// All internal types (PackPlan, PackFileEntry, pack::detail::) remain invisible to consumers.

#include "pack/pack.h"
#include "pack/pack_types.h"
#include "pack/pack_plan_internal.h"
#include "pack/packer_types.h"
#include "pack/pack_service.h"
#include "pack/pack_internal.h"
#include "pack/packer.h"
#include "core/collision_naming.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>

namespace naming = collisionnaming;

namespace pack {

namespace {

// ponytail: ASan workaround — extract optional to raw pointer before use
// to prevent compiler speculative read across inline boundaries.
[[gnu::noinline]] auto optNamingPtr(PackRequest const& r) -> NamingConfig const* {
  return r.naming.has_value() ? &r.naming.value() : nullptr;
}

// --- runNonResumable ---
// Packs all groups in the given plan using a fresh PackService/Packer pair.
// Returns PackRunResult on success or eh::Result error on failure.
auto runNonResumable(PackPlan const& plan) -> eh::Result<PackRunResult> {
  PackService svc;
  auto const packRes = svc.packGroups(plan);
  if (!packRes) { return eh::makeError("{}", packRes.error()); }
  return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
}

// --- buildPackZipBaseName ---
// Internalized from picture's buildPicturePackBaseName. Builds zip file base
// name from part/subPart indices and optional baseName prefix.
auto buildPackZipBaseName(
  std::string_view baseName,
  std::size_t partIndex,
  std::size_t subPartIndex,
  std::size_t totalSubParts
) -> std::string {
  std::string zipBase;
  if (!baseName.empty()) {
    zipBase = std::format("{}_part{}", baseName, partIndex);
  } else {
    zipBase = std::format("part{}", partIndex);
  }
  if (totalSubParts > 1) {
    zipBase += std::format(".{}.zip", subPartIndex + 1);
  } else {
    zipBase += ".zip";
  }
  return zipBase;
}

// --- makeDefaultZipNameStrategy ---
// Returns a zipNameForIndex lambda using default mode-based naming.
// Captures shared state for ordinal ranges and subPart tracking.
auto makeDefaultZipNameStrategy(
  std::string baseName,
  std::vector<FileOrdinalRange> ordinalRanges,
  std::vector<std::pair<std::size_t, std::size_t>> groupNameParts,
  std::vector<std::size_t> subPartCountsByPart
) -> std::function<std::string(std::size_t)> {
  struct NamingState {
    std::string baseName;
    std::vector<FileOrdinalRange> ordinalRanges;
    std::vector<std::pair<std::size_t, std::size_t>> groupNameParts;
    std::vector<std::size_t> subPartCountsByPart;
  };
  auto state = std::make_shared<NamingState>(NamingState{
    std::move(baseName),
    std::move(ordinalRanges),
    std::move(groupNameParts),
    std::move(subPartCountsByPart)
  });

  return [state](std::size_t index) -> std::string {
    auto const [partIndex, subPartIndex] = state->groupNameParts.at(index);
    auto const totalSubParts = state->subPartCountsByPart.at(partIndex - 1);
    auto const zipBase =
      buildPackZipBaseName(state->baseName, partIndex, subPartIndex, totalSubParts);
    return pack::internal::appendOrdinalRangeSuffix(
      zipBase,
      state->ordinalRanges.at(index)
    );
  };
}

constexpr auto kMaxEntriesPerPart = std::size_t{2000};

// --- collectPackInputs ---
// Converts PackRequest entries (or entryInputs pass-through) into a
// PackEntryInput vector, resolving naming strategy and computing
// the common ancestor root for NamingStrategy::Keep.
auto collectPackInputs(PackRequest const& request)
  -> std::vector<pack::detail::PackEntryInput> {
  // Pass-through: if entryInputs provided, return as-is
  if (!request.entryInputs.empty()) { return request.entryInputs; }

  auto const* naming = optNamingPtr(request);
  auto const namingStrategy = naming ? naming->namingStrategy : NamingStrategy::Flat;

  // Compute common ancestor directory for Keep strategy
  auto commonRoot = fs::path{};
  if (namingStrategy == NamingStrategy::Keep && !request.entries.empty()) {
    commonRoot = request.entries[0].parent_path();
    for (
      auto i = std::size_t{1}; i < request.entries.size() && !commonRoot.empty(); ++i
    ) {
      auto const& other = request.entries[i].parent_path();
      auto it1 = commonRoot.begin();
      auto const end1 = commonRoot.end();
      auto it2 = other.begin();
      auto const end2 = other.end();
      auto newRoot = fs::path{};
      for (; it1 != end1 && it2 != end2 && *it1 == *it2; ++it1, ++it2) {
        newRoot /= *it1;
      }
      commonRoot = std::move(newRoot);
    }
  }

  auto packInputs = std::vector<pack::detail::PackEntryInput>{};
  packInputs.reserve(request.entries.size());
  for (auto const& entry: request.entries) {
    auto zipName = std::string{};
    switch (namingStrategy) {
      case NamingStrategy::Flat: zipName = entry.filename().generic_string(); break;
      case NamingStrategy::FlatWithForce:
        zipName = naming::buildConflictHandledFlatName(
          entry.parent_path(),
          entry,
          entry.stem().string(),
          entry.extension().string()
        );
        break;
      case NamingStrategy::Keep:
        if (commonRoot.empty()) {
          zipName = entry.filename().generic_string();
        } else {
          zipName = entry.lexically_relative(commonRoot).generic_string();
        }
        break;
    }
    packInputs.push_back({
      .entry =
        pack::PackFileEntry{
          .sourcePath = entry,
          .zipEntryName = std::move(zipName),
        },
      .sourceDir = entry.parent_path(),
      .sourceKey = naming::stablePathString(entry.parent_path()),
      .fileKey = naming::stablePathString(entry),
    });
  }
  return packInputs;
}

// --- appendSummaryEntries ---
// Appends summary PackEntryInput records to the packInputs vector when
// SummaryConfig is enabled.
void appendSummaryEntries(
  PackRequest const& request,
  std::vector<pack::detail::PackEntryInput>& packInputs
) {
  if (!request.summary.has_value() || !request.summary->enabled) { return; }
  for (auto const& summaryEntry: request.summary->entries) {
    packInputs.push_back({
      .entry =
        pack::PackFileEntry{
          .sourcePath = summaryEntry.sourcePath,
          .zipEntryName = summaryEntry.zipEntryName,
          .isSummary = true,
        },
      .sourceDir = summaryEntry.sourcePath.parent_path(),
      .sourceKey = naming::stablePathString(summaryEntry.sourcePath.parent_path()),
      .fileKey = naming::stablePathString(summaryEntry.sourcePath),
      .isSummary = true,
    });
  }
}

// --- resolveKeepTogetherThreshold ---
// Maps GroupingStrategy to the keep-together threshold value used by
// two-layer partitioning.
auto resolveKeepTogetherThreshold(PackRequest const& request)
  -> std::optional<std::size_t> {
  switch (request.groupingStrategy) {
    case GroupingStrategy::PerSourceDir:
      return std::optional<std::size_t>{kMaxEntriesPerPart};
    case GroupingStrategy::PerSourceDirKeepTogether: return std::optional<std::size_t>{0};
  }
}

// --- partitionPackInputs ---
// Runs two-layer partitioning via Packer, converts partitions to grouped
// entries with subPart tracking, and stable_partitions summary entries to
// the front of each group.
auto partitionPackInputs(
  std::vector<pack::detail::PackEntryInput> const& packInputs,
  std::optional<std::size_t> keepTogetherThreshold
)
  -> std::tuple<
    std::vector<std::vector<PackFileEntry>>,
    std::vector<std::pair<std::size_t, std::size_t>>,
    std::vector<std::size_t>
  > {
  Packer packer;
  auto const partitions = packer.groupPackEntriesWithSubparts(
    packInputs,
    pack::kDefaultMaxArchiveGroupSize,
    kMaxEntriesPerPart,
    keepTogetherThreshold
  );

  auto groupedEntries = std::vector<std::vector<PackFileEntry>>{};
  auto groupNameParts = std::vector<std::pair<std::size_t, std::size_t>>{};
  auto subPartCountsByPart = std::vector<std::size_t>{};
  groupedEntries.reserve(partitions.size());
  groupNameParts.reserve(partitions.size());
  for (auto const& partition: partitions) {
    groupedEntries.push_back(partition.entries);
    groupNameParts.emplace_back(partition.partIndex, partition.subPartIndex);
    if (subPartCountsByPart.size() < partition.partIndex) {
      subPartCountsByPart.resize(partition.partIndex, 0);
    }
    ++subPartCountsByPart[partition.partIndex - 1];
  }

  for (auto& group: groupedEntries) {
    std::ranges::stable_partition(group, [](PackFileEntry const& e) {
      return e.isSummary;
    });
  }

  return {
    std::move(groupedEntries),
    std::move(groupNameParts),
    std::move(subPartCountsByPart)
  };
}

// --- applyEntryNameOverrides ---
// Applies the entryNameForFile callback to override zip entry names when
// entries are provided as raw paths (not via entryInputs).
void applyEntryNameOverrides(
  PackRequest const& request,
  std::vector<std::vector<PackFileEntry>>& groups
) {
  if (request.entryInputs.empty() && request.entryNameForFile) {
    for (auto& group: groups) {
      for (auto& entry: group) {
        if (entry.isSummary) { continue; }
        entry.zipEntryName = request.entryNameForFile(entry.sourcePath);
      }
    }
  }
}

// --- resolveZipNameStrategy ---
// Returns a zipNameForIndex lambda. Uses the consumer-provided
// zipNameStrategy if present; otherwise falls back to the default mode-based
// naming via makeDefaultZipNameStrategy.
auto resolveZipNameStrategy(
  NamingConfig const* naming,
  std::string baseName,
  std::vector<FileOrdinalRange> const& ordinalRanges,
  std::vector<std::pair<std::size_t, std::size_t>> groupNameParts,
  std::vector<std::size_t> subPartCountsByPart
) -> std::function<std::string(std::size_t)> {
  if (naming && naming->zipNameStrategy) {
    return [strategy = naming->zipNameStrategy,
            ordRanges = ordinalRanges,
            nameParts = groupNameParts,
            subPartCounts = subPartCountsByPart,
            bName = baseName](std::size_t index) {
      auto const [partIndex, subPartIndex] = nameParts.at(index);
      auto const totalSubParts = subPartCounts.at(partIndex - 1);
      return strategy(partIndex, subPartIndex, totalSubParts, bName, ordRanges.at(index));
    };
  }

  return makeDefaultZipNameStrategy(
    std::move(baseName),
    ordinalRanges,
    std::move(groupNameParts),
    std::move(subPartCountsByPart)
  );
}

// --- buildMediaPackPlan ---
// Groups entries using two-layer partitioning (groupPackEntriesWithSubparts),
// reads NamingConfig for baseName and zipNameStrategy, applies entryNameForFile
// callback, and returns a PackPlan ready for execution.
auto buildMediaPackPlan(PackRequest const& request) -> eh::Result<PackPlan> {
  auto const* naming = optNamingPtr(request);

  // 1. Build packInputs + append summary
  auto packInputs = collectPackInputs(request);
  appendSummaryEntries(request, packInputs);
  // 2. Partition + group
  auto const keepTogether = resolveKeepTogetherThreshold(request);
  auto [groups, nameParts, subPartCounts] = partitionPackInputs(packInputs, keepTogether);
  // 3. Entry name overrides
  applyEntryNameOverrides(request, groups);
  // 4. Naming configuration
  auto baseName = std::string{};
  if (naming && naming->baseName.has_value()) { baseName = naming->baseName.value(); }
  auto const ordinalRanges = pack::internal::buildGroupOrdinalRanges(groups);
  auto zipNameFn = resolveZipNameStrategy(
    naming,
    baseName,
    ordinalRanges,
    std::move(nameParts),
    std::move(subPartCounts)
  );
  return PackPlan{
    .groups = std::move(groups),
    .outputDir = request.outputDir,
    .zipNameForIndex = std::move(zipNameFn),
    .maxParallelJobs = request.maxParallelJobs,
    .removeOnFailure = request.removeOnFailure,
    .compact = request.compact,
  };
}

// --- runResumable ---
// D-11: Internalized archive_plan logic. Takes a JobState store, merges tasks,
// filters for needsExecution, sets progress callbacks, and runs packing.
// Archive task records for every group: one task per output zip.
auto buildArchiveTaskRecords(PackPlan const& plan) -> std::vector<jobstate::TaskRecord> {
  auto allIndexes = std::vector<std::size_t>(plan.groups.size());
  std::iota(allIndexes.begin(), allIndexes.end(), std::size_t{0});

  auto archiveTasks = std::vector<jobstate::TaskRecord>{};
  archiveTasks.reserve(plan.groups.size());
  for (auto const index: allIndexes) {
    auto const zipName = pack::internal::resolveZipNameForIndex(plan, index);
    auto const label = pack::internal::resolveProgressLabelForIndex(plan, index);
    auto members = std::vector<fs::path>{};
    members.reserve(plan.groups[index].size());
    for (auto const& entry: plan.groups[index]) { members.push_back(entry.sourcePath); }
    archiveTasks
      .push_back(jobstate::makeArchiveTask(plan.outputDir / zipName, members, label));
  }
  return archiveTasks;
}

// Shared state for progress callbacks
struct ResumableState {
  jobstate::Store* store;
  std::vector<jobstate::TaskRecord> mergedTasks;
  std::vector<std::size_t> pendingIndexes;
};

auto makeResumableProgressCallbacks(
  jobstate::Store& store,
  std::vector<jobstate::TaskRecord> const& mergedTasks,
  std::vector<std::size_t> const& pendingIndexes
) -> pack::PackProgressCallbacks {
  auto resumableState = std::make_shared<ResumableState>(ResumableState{
    .store = &store,
    .mergedTasks = mergedTasks,
    .pendingIndexes = pendingIndexes
  });
  auto callbacks = pack::PackProgressCallbacks{};
  callbacks.onGroupStart = [resumableState](std::size_t subsetIndex) {
    auto const& id =
      resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
    resumableState->store->markRunning(id);
  };
  callbacks.onGroupSuccess = [resumableState](std::size_t subsetIndex, fs::path const&) {
    auto const& id =
      resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
    resumableState->store->markSucceeded(id);
  };
  callbacks.onGroupFailure =
    [resumableState](std::size_t subsetIndex, std::string const& error) {
      auto const& id =
        resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
      resumableState->store->markFailed(id, error);
    };
  return callbacks;
}

auto runResumable(PackPlan const& plan, jobstate::Store& store)
  -> eh::Result<PackRunResult> {
  // Build archive task records for all groups, merge with existing job state
  // and filter down to the indexes that still need execution.
  auto const mergedTasks = store.mergeTasks(buildArchiveTaskRecords(plan));
  auto pendingActionIds = std::vector<std::string>{};
  auto pendingIndexes = std::vector<std::size_t>{};
  for (auto i = std::size_t{0}; i < mergedTasks.size(); ++i) {
    if (!jobstate::needsExecution(mergedTasks[i])) { continue; }
    pendingIndexes.push_back(i);
    pendingActionIds.push_back(mergedTasks[i].id);
  }

  // All already complete?
  if (pendingIndexes.empty()) {
    store.setStage("completed");
    return PackRunResult{.exitCode = 0, .zippedFiles = {}};
  }

  store.setStage("packing");

  // Build filtered PackPlan (replicating selectPackPlanIndexes logic)
  auto pendingPlan = pack::internal::selectPackPlanIndexes(plan, pendingIndexes);
  pendingPlan.progressCallbacks =
    makeResumableProgressCallbacks(store, mergedTasks, pendingIndexes);

  // Execute the filtered plan
  PackService svc2;
  auto const packRes = svc2.packGroups(pendingPlan);
  if (!packRes) {
    if (stopsignal::isStopRequested()) {
      store.requestCancel();
      store.markIncompleteInterrupted(pendingActionIds);
      return PackRunResult{.exitCode = stopsignal::kCanceledExitCode};
    }
    return eh::makeError("{}", packRes.error());
  }

  store.setStage("completed");
  return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
}

}  // namespace

auto execute(PackPlan const& plan, jobstate::Store* jobState)
  -> eh::Result<PackRunResult> {
  if (jobState == nullptr) { return runNonResumable(plan); }
  return runResumable(plan, *jobState);
}

// --- execute() — public entry point ---
auto execute(PackRequest const& request) -> eh::Result<PackRunResult> {
  // --- Build PackPlan ---
  auto plan = PackPlan{};

  if (request.mode == PackMode::Directory) {
    if (request.entries.empty()) {
      return eh::makeError(
        "Directory mode requires at least one entry (the directory path)."
      );
    }

    Packer packer;
    auto const* naming = optNamingPtr(request);
    auto const namingStrategy = naming ? naming->namingStrategy : NamingStrategy::Flat;
    auto const planRes = packer.buildDirectoryPackPlan(
      request.entries[0],  // directory path
      request.outputDir,
      kDefaultMaxArchiveGroupSize,
      request.recursive,
      namingStrategy,
      request.maxParallelJobs,
      std::nullopt  // excludedPath — not needed for pack-only
    );

    if (!planRes) { return eh::makeError("{}", planRes.error()); }

    plan = planRes.value();
    plan.compact = request.compact;
    plan.maxParallelJobs = request.maxParallelJobs;
    plan.removeOnFailure = request.removeOnFailure;
  } else {
    // --- Media mode (PackMode::Media) ---
    if (request.entries.empty() && request.entryInputs.empty()) {
      return PackRunResult{.exitCode = 0, .zippedFiles = {}};
    }

    // Ensure output directory exists
    auto ec = std::error_code{};
    fs::create_directories(request.outputDir, ec);

    auto const planRes = buildMediaPackPlan(request);
    if (!planRes) { return eh::makeError("{}", planRes.error()); }
    plan = planRes.value();
  }

  // --- Execute: resumable or non-resumable ---
  auto const result = execute(plan, request.jobState);

  // For Directory mode, print success message after resumable completion too
  if (result && result->exitCode == 0 && request.mode == PackMode::Directory) {
    terminal::println(
      terminal::MessageKind::Success,
      "All files packed successfully to: {}",
      terminal::path(request.outputDir)
    );
  }

  return result;
}

}  // namespace pack
