#include "app/pipeline.h"

#include "core/archive_plan.h"
#include "core/job_state.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "pack/packer.h"
#include "picture/picture_process.h"
#include "utils/utils.h"
#include "video/video_process.h"

#include <filesystem>

namespace fs = std::filesystem;

using enum terminal::MessageKind;

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
    terminal::println(Info, "Resuming job state from: {}", terminal::path(stateFilePath));
  }

  return {};
}

auto runPackPlan(appctx::AppContext& ctx, pack::PackPlan const& plan) -> eh::Result<int> {
  auto* store = ctx.runtime.jobState.get();
  if (store == nullptr) {
    auto const packRes = pack::packGroupsParallel(plan);
    if (!packRes) { return eh::makeError("{}", packRes.error()); }
    return 0;
  }

  auto preparedExecution = archiveplan::prepareResumablePackExecution(*store, plan);
  if (!preparedExecution.pendingPlan.has_value()) {
    store->setStage("completed");
    return 0;
  }

  store->setStage("packing");

  auto const packRes = pack::packGroupsParallel(preparedExecution.pendingPlan.value());
  if (!packRes) {
    if (stopsignal::isStopRequested()) {
      store->requestCancel();
      store->markIncompleteInterrupted(preparedExecution.pendingActionIds);
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

  terminal::println(
    Success,
    "All files packed successfully to: {}",
    terminal::path(zipOutputDir)
  );
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
  terminal::println(
    Info,
    "Scanning input path for pictures: {} ...",
    terminal::path(ctx.config.inputPath)
  );
  auto const scannedPics = readAllPics(ctx.config, ctx.config.inputPath);
  auto const planRes =
    buildPicturePackPlan(ctx.config, ctx.config.inputPath, outputDir, scannedPics);
  if (!planRes) { return eh::makeError("Failed to pack pictures: {}", planRes.error()); }

  terminal::println(
    Info,
    "Picture scan completed, {} picture(s) found, grouped into {} package batch(es).",
    terminal::count(scannedPics.size()),
    terminal::count(planRes->groups.size())
  );
  auto const proceed = readUserIpt(
    ctx.config.yesToAll,
    "do you want to proceed with packing the pictures? (y/N): "
  );
  if (!proceed) {
    terminal::println(Warning, "Packing task canceled by user.");
    return 0;
  }

  auto const packRes = runPackPlan(ctx, planRes.value());

  if (!packRes) { return eh::makeError("Failed to pack pictures: {}", packRes.error()); }
  if (packRes.value() != 0) { return packRes.value(); }

  terminal::println(
    Success,
    "All pictures packed successfully to: {}",
    terminal::path(outputDir)
  );
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
