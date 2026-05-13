#include "app/pipeline.h"

#include "core/job_state.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "pack/pack.h"
#include "picture/picture_process.h"
#include "video/video_process.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

using enum terminal::MessageKind;

namespace pipeline {

namespace {

auto toNamingStrategy(appctx::AppConfig const& config) -> pack::NamingStrategy {
  if (config.outputLayout == appctx::OutputLayout::Keep) {
    return pack::NamingStrategy::Keep;
  }
  if (config.forceNameConflictHandling) { return pack::NamingStrategy::FlatWithForce; }
  return pack::NamingStrategy::Flat;
}

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

auto jobStateNeverStarted(jobstate::Store const& store) -> bool {
  auto const tasks = store.tasks();
  return std::ranges::all_of(tasks, [](jobstate::TaskRecord const& task) {
    return task.status == jobstate::TaskStatus::Pending && task.attemptCount == 0;
  });
}

auto maybeRemoveUnstartedCanceledJobState(
  appctx::AppContext& ctx,
  eh::Result<int> const& runRes
) -> void {
  if (!runRes || runRes.value() != stopsignal::kCanceledExitCode) { return; }
  if (!ctx.runtime.jobState) { return; }
  if (!jobStateNeverStarted(*ctx.runtime.jobState)) { return; }

  auto ec = std::error_code{};
  fs::remove(ctx.runtime.jobState->stateFilePath(), ec);
  ctx.runtime.jobState.reset();
}

auto runPackOnly(appctx::AppContext& ctx) -> eh::Result<int> {
  if (ctx.config.processType != "video") {
    return eh::makeError("pack-only option is only supported when --type is video.");
  }

  if (!fs::is_directory(ctx.config.inputPath)) {
    return eh::makeError("pack-only mode requires input to be a directory.");
  }

  auto const outputDir = ctx.config.outputPath.value_or(ctx.config.inputPath) / "packed";

  auto const packRes = pack::execute({
    .entries = {ctx.config.inputPath},
    .mode = pack::PackMode::Directory,
    .outputDir = outputDir,
    .compact = !ctx.config.fullProgress,
    .naming =
      pack::NamingConfig{
        .namingStrategy = toNamingStrategy(ctx.config),
      },
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
  });

  if (!packRes) { return eh::makeError("{}", packRes.error()); }
  return packRes->exitCode;
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

  return runPicturePackWorkflow(ctx, ctx.config.inputPath);
}

}  // namespace

auto run(appctx::AppContext& ctx) -> eh::Result<int> {
  if (shouldEnableJobState(ctx.config)) {
    auto const stateRes = ensureJobState(ctx);
    if (!stateRes) { return eh::makeError("{}", stateRes.error()); }
  }

  auto runRes = eh::Result<int>{};

  if (ctx.config.packOnly) {
    runRes = runPackOnly(ctx);
    maybeRemoveUnstartedCanceledJobState(ctx, runRes);
    return runRes;
  }

  if (ctx.config.processType == "video") {
    runRes = runVideo(ctx);
    maybeRemoveUnstartedCanceledJobState(ctx, runRes);
    return runRes;
  }

  if (ctx.config.processType == "picture") {
    runRes = runPicture(ctx);
    maybeRemoveUnstartedCanceledJobState(ctx, runRes);
    return runRes;
  }

  return eh::makeError(
    "Invalid process type: {}. Valid types are: video, picture.",
    ctx.config.processType
  );
}

}  // namespace pipeline
