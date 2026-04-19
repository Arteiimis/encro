#include "core/pipeline.h"

#include "core/job_state.h"
#include "core/stop_signal.h"
#include "pack/packer.h"
#include "pack/picture_process.h"
#include "utils/utils.h"
#include "video/video_process.h"

#include <filesystem>
#include <print>

namespace fs = std::filesystem;

namespace pipeline {

namespace {

auto shouldEnableJobState(appctx::AppConfig const& config) -> bool {
  if (config.processType == "video" && !config.packOnly) { return true; }

  return config.resumeState || config.restartState || config.stateFilePath.has_value();
}

auto ensureJobState(appctx::AppContext& ctx) -> eh::Result<void> {
  if (ctx.runtime.jobState) { return {}; }

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  ctx.runtime.jobState = std::make_shared<jobstate::Store>(stateFilePath);
  auto const initRes =
    ctx.runtime.jobState->initialize(ctx.config, ctx.config.restartState);
  if (!initRes) { return eh::makeError("{}", initRes.error()); }

  if (initRes.value()) {
    std::println("Resuming job state from: {}", stateFilePath.string());
  }

  return {};
}

auto buildArchiveActions(pack::PackPlan const& plan, std::span<std::size_t const> indexes)
  -> std::vector<jobstate::ActionRecord> {
  auto actions = std::vector<jobstate::ActionRecord>{};
  actions.reserve(indexes.size());

  for (auto const index: indexes) {
    auto const zipName = plan.zipNameForIndex ? plan.zipNameForIndex(index)
                                              : std::format("part{}.zip", index + 1);
    auto const label = plan.progressLabelForIndex ? plan.progressLabelForIndex(index)
                                                  : std::format("Packing: {}", zipName);
    actions.push_back(
      jobstate::makeArchiveAction(plan.outputDir / zipName, plan.groups[index], label)
    );
  }

  return actions;
}

auto selectPackPlanIndexes(
  pack::PackPlan const& plan,
  std::vector<std::size_t> const& indexes
) -> pack::PackPlan {
  auto filteredGroups = std::vector<std::vector<fs::path>>{};
  filteredGroups.reserve(indexes.size());
  for (auto const index: indexes) { filteredGroups.push_back(plan.groups[index]); }

  return pack::PackPlan{
    .groups = std::move(filteredGroups),
    .outputDir = plan.outputDir,
    .zipNameForIndex =
      [base = plan.zipNameForIndex, indexes](std::size_t subsetIndex) {
        auto const actualIndex = indexes.at(subsetIndex);
        return base ? base(actualIndex) : std::format("part{}.zip", actualIndex + 1);
      },
    .progressLabelForIndex =
      [base = plan.progressLabelForIndex,
       zipName = plan.zipNameForIndex,
       indexes](std::size_t subsetIndex) {
        auto const actualIndex = indexes.at(subsetIndex);
        if (base) { return base(actualIndex); }
        auto const resolvedZipName =
          zipName ? zipName(actualIndex) : std::format("part{}.zip", actualIndex + 1);
        return std::format("Packing: {}", resolvedZipName);
      },
    .zipEntryNameForFile = plan.zipEntryNameForFile,
    .onGroupStart = {},
    .onGroupSuccess = {},
    .onGroupFailure = {},
    .maxParallelJobs = plan.maxParallelJobs,
    .removeOnFailure = plan.removeOnFailure,
  };
}

auto runPackPlan(appctx::AppContext& ctx, pack::PackPlan const& plan) -> eh::Result<int> {
  auto* store = ctx.runtime.jobState.get();
  if (store == nullptr) {
    auto const packRes = pack::packGroupsParallel(plan);
    if (!packRes) { return eh::makeError("{}", packRes.error()); }
    return 0;
  }

  auto allIndexes = std::vector<std::size_t>{};
  allIndexes.reserve(plan.groups.size());
  for (auto index = std::size_t{0}; index < plan.groups.size(); ++index) {
    allIndexes.push_back(index);
  }

  auto const mergedActions = store->mergeActions(buildArchiveActions(plan, allIndexes));
  auto pendingIndexes = std::vector<std::size_t>{};
  auto pendingActionIds = std::vector<std::string>{};
  pendingIndexes.reserve(mergedActions.size());
  pendingActionIds.reserve(mergedActions.size());
  for (auto index = std::size_t{0}; index < mergedActions.size(); ++index) {
    if (!jobstate::needsExecution(mergedActions[index])) { continue; }
    pendingIndexes.push_back(index);
    pendingActionIds.push_back(mergedActions[index].id);
  }

  if (pendingIndexes.empty()) {
    store->setStage("completed");
    return 0;
  }

  store->setStage("packing");

  auto filteredPlan = selectPackPlanIndexes(plan, pendingIndexes);
  filteredPlan.onGroupStart =
    [store, mergedActions, pendingIndexes](std::size_t subsetIndex) {
      store->markRunning(mergedActions[pendingIndexes.at(subsetIndex)].id);
    };
  filteredPlan.onGroupSuccess =
    [store, mergedActions, pendingIndexes](std::size_t subsetIndex, fs::path const&) {
      store->markSucceeded(mergedActions[pendingIndexes.at(subsetIndex)].id);
    };
  filteredPlan.onGroupFailure =
    [store,
     mergedActions,
     pendingIndexes](std::size_t subsetIndex, std::string const& error) {
      store->markFailed(mergedActions[pendingIndexes.at(subsetIndex)].id, error);
    };

  auto const packRes = pack::packGroupsParallel(filteredPlan);
  if (!packRes) {
    if (stopsignal::isStopRequested()) {
      store->requestCancel();
      store->markIncompleteInterrupted(pendingActionIds);
      return stopsignal::kCanceledExitCode;
    }
    return eh::makeError("{}", packRes.error());
  }

  store->setStage("completed");
  return 0;
}

auto runPackOnly(appctx::AppContext& ctx) -> eh::Result<int> {
  if (ctx.config.processType != "video") {
    return eh::makeError("pack-only option is only supported when --type is video.");
  }

  if (!fs::is_directory(ctx.config.inputPath)) {
    return eh::makeError("pack-only mode requires input to be a directory.");
  }

  auto const zipOutputDir =
    ctx.config.outputPath.value_or(ctx.config.inputPath / "packed");
  auto const planRes = buildDirectoryPackPlan(
    ctx.config.inputPath,
    zipOutputDir,
    500 * 1024 * 1024,
    true,
    ctx.config.forceNameConflictHandling,
    ctx.config.maxParallelJobs,
    ctx.runtime.jobState ? std::optional<fs::path>{ctx.runtime.jobState->stateFilePath()}
                         : std::nullopt
  );
  if (!planRes) { return eh::makeError("Failed to pack files: {}", planRes.error()); }

  auto const packRes = runPackPlan(ctx, planRes.value());
  if (!packRes) { return eh::makeError("Failed to pack files: {}", packRes.error()); }
  if (packRes.value() != 0) { return packRes.value(); }

  std::println("All files packed successfully to: {}", zipOutputDir.string());
  return 0;
}

auto runVideo(appctx::AppContext& ctx) -> eh::Result<int> {
  if (!ctx.config.inputPaths.empty()) {
    return handleMultiFileEncoding(ctx, ctx.config.inputPaths);
  }

  auto const& inputPath = ctx.config.inputPath;

  if (fs::is_directory(inputPath)) { return handlePathEncoding(ctx, inputPath); }

  if (fs::is_regular_file(inputPath)) { return handleSingleFileEncoding(ctx, inputPath); }

  return eh::makeError(
    "The specified path is neither a directory nor a regular file: {}",
    inputPath.string()
  );
}

auto runPicture(appctx::AppContext& ctx) -> eh::Result<int> {
  if (!fs::is_directory(ctx.config.inputPath)) {
    return eh::makeError(
      "The specified path is neither a directory nor a regular file: {}",
      ctx.config.inputPath.string()
    );
  }

  auto const outputDir = ctx.config.outputPath.value_or(ctx.config.inputPath) / "packed";
  std::println("Scanning input path for pictures: {} ...", ctx.config.inputPath.string());
  auto const scannedPics = readAllPics(ctx.config, ctx.config.inputPath);
  auto const planRes =
    buildPicturePackPlan(ctx.config, ctx.config.inputPath, outputDir, scannedPics);
  if (!planRes) { return eh::makeError("Failed to pack pictures: {}", planRes.error()); }

  std::println(
    "Picture scan completed, {} picture(s) found, grouped into {} package batch(es).",
    scannedPics.size(),
    planRes->groups.size()
  );
  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    std::println("Packing task canceled by user.");
    return 0;
  }

  auto const packRes = runPackPlan(ctx, planRes.value());

  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes.value() != 0) { return packRes.value(); }

  std::println("All pictures packed successfully to: {}", outputDir.string());
  return 0;
}

}  // namespace

auto run(appctx::AppContext& ctx) -> eh::Result<int> {
  if (shouldEnableJobState(ctx.config)) {
    auto const stateRes = ensureJobState(ctx);
    if (!stateRes) { return eh::makeError("{}", stateRes.error()); }
  }

  if (ctx.config.packOnly) { return runPackOnly(ctx); }

  if (ctx.config.processType == "video") { return runVideo(ctx); }

  if (ctx.config.processType == "picture") { return runPicture(ctx); }

  return eh::makeError(
    "Invalid process type: {}. Valid types are: video, picture.",
    ctx.config.processType
  );
}

}  // namespace pipeline
