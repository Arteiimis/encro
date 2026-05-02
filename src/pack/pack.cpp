// pack::execute() — Task 1 + Task 2: non-resumable + resumable paths
//
// Single entry point D-05. Internally manages Packer/PackService lifecycle,
// handles grouping + naming + PackPlan construction, and runs packing.
// Resumable execution (D-11) is fully internalized here.
// All internal types (PackPlan, PackFileEntry, pack::detail::) remain invisible to consumers.

#include "pack/pack.h"
#include "pack/pack_types.h"
#include "pack/packer_types.h"
#include "pack/pack_service.h"
#include "pack/pack_internal.h"
#include "pack/packer.h"
#include "core/collision_naming.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"

#include <spdlog/spdlog.h>

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

// --- buildMediaPackPlan ---
// Groups entries using two-layer partitioning (groupPackEntriesWithSubparts),
// reads NamingConfig for baseName and zipNameStrategy, applies entryNameForFile
// callback, and returns a PackPlan ready for execution.
auto buildMediaPackPlan(PackRequest const& request) -> eh::Result<PackPlan> {
  constexpr auto kMaxEntriesPerPart = std::size_t{2000};

  auto packInputs = std::vector<pack::detail::PackEntryInput>{};
  if (!request.entryInputs.empty()) {
    packInputs = request.entryInputs;
  } else {
    packInputs.reserve(request.entries.size());
    for (auto const& entry: request.entries) {
      packInputs.emplace_back(
        pack::detail::PackEntryInput{
          .entry =
            pack::PackFileEntry{
              .sourcePath = entry,
              .zipEntryName = entry.filename().generic_string(),
            },
          .sourceDir = entry.parent_path(),
          .sourceKey = naming::stablePathString(entry.parent_path()),
          .fileKey = naming::stablePathString(entry),
        }
      );
    }
  }

  Packer packer;
  auto const partitions = packer.groupPackEntriesWithSubparts(
    packInputs,
    kDefaultMaxArchiveGroupSize,
    kMaxEntriesPerPart,
    kMaxEntriesPerPart
  );

  // Convert partitions to grouped entries with subPart tracking
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

  // Apply entryNameForFile callback to override zip entry names
  if (request.entryInputs.empty() && request.entryNameForFile) {
    for (auto& group: groupedEntries) {
      for (auto& entry: group) {
        entry.zipEntryName = request.entryNameForFile(entry.sourcePath);
      }
    }
  }

  // Extract baseName from NamingConfig
  auto baseName = std::string{};
  if (request.naming.has_value() && request.naming->baseName.has_value()) {
    baseName = request.naming->baseName.value();
  }

  auto const ordinalRanges = pack::internal::buildGroupOrdinalRanges(groupedEntries);

  // Build zip name lambda: consumer-provided strategy or default
  std::function<std::string(std::size_t)> zipNameForIndex;

  if (request.naming.has_value() && request.naming->zipNameStrategy) {
    auto strategy = request.naming->zipNameStrategy;
    auto ordRanges = ordinalRanges;
    auto nameParts = groupNameParts;
    auto subPartCounts = subPartCountsByPart;
    auto bName = baseName;
    zipNameForIndex =  //
      [strategy, ordRanges, nameParts, subPartCounts, bName](std::size_t index) {
        auto const [partIndex, subPartIndex] = nameParts.at(index);
        auto const totalSubParts = subPartCounts.at(partIndex - 1);
        return strategy(
          partIndex,
          subPartIndex,
          totalSubParts,
          bName,
          ordRanges.at(index)
        );
      };
  } else {
    zipNameForIndex = makeDefaultZipNameStrategy(
      baseName,
      ordinalRanges,
      std::move(groupNameParts),
      std::move(subPartCountsByPart)
    );
  }

  return PackPlan{
    .groups = std::move(groupedEntries),
    .outputDir = request.outputDir,
    .zipNameForIndex = std::move(zipNameForIndex),
    .maxParallelJobs = request.maxParallelJobs,
    .removeOnFailure = request.removeOnFailure,
    .compact = request.compact,
  };
}

// --- runResumable ---
// D-11: Internalized archive_plan logic. Takes a JobState store, merges tasks,
// filters for needsExecution, sets progress callbacks, and runs packing.
auto runResumable(PackPlan const& plan, jobstate::Store& store)
  -> eh::Result<PackRunResult> {
  // Build archive task records for all groups
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

  // Merge with existing job state
  auto const mergedTasks = store.mergeTasks(archiveTasks);

  // Filter: which indexes need execution?
  auto pendingIndexes = std::vector<std::size_t>{};
  pendingIndexes.reserve(mergedTasks.size());
  auto pendingActionIds = std::vector<std::string>{};
  pendingActionIds.reserve(mergedTasks.size());
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

  // Shared state for progress callbacks
  struct ResumableState {
    jobstate::Store* store;
    std::vector<jobstate::TaskRecord> mergedTasks;
    std::vector<std::size_t> pendingIndexes;
  };
  auto resumableState = std::make_shared<ResumableState>(ResumableState{
    &store,
    std::move(mergedTasks),
    std::move(pendingIndexes)
  });

  pendingPlan.progressCallbacks.onGroupStart = [resumableState](std::size_t subsetIndex) {
    auto const& id =
      resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
    resumableState->store->markRunning(id);
  };
  pendingPlan.progressCallbacks.onGroupSuccess =
    [resumableState](std::size_t subsetIndex, fs::path const&) {
      auto const& id =
        resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
      resumableState->store->markSucceeded(id);
    };
  pendingPlan.progressCallbacks.onGroupFailure =
    [resumableState](std::size_t subsetIndex, std::string const& error) {
      auto const& id =
        resumableState->mergedTasks[resumableState->pendingIndexes.at(subsetIndex)].id;
      resumableState->store->markFailed(id, error);
    };

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
    auto const planRes = packer.buildDirectoryPackPlan(
      request.entries[0],  // directory path
      request.outputDir,
      kDefaultMaxArchiveGroupSize,
      request.recursive,
      request.naming.has_value() ? request.naming->forceConflictHandling : false,
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
