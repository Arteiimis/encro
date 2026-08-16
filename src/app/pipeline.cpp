#include "app/pipeline.h"

#include "core/job_state.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "pack/pack.h"
#include "picture/picture_process.h"
#include "video/video_process.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

using enum terminal::MessageKind;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::APP_PIPELINE);

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
  if (config.dryRun) { return false; }  // dry-run leaves no state behind
  if (config.processType == "video" && !config.packOnly) { return true; }
  if (config.processType == "picture" && config.compressImages) { return true; }

  return config.resumeState || config.restartState || config.stateFilePath.has_value();
}

auto ensureJobState(appctx::AppContext& ctx) -> eh::Result<void> {
  if (ctx.runtime.jobState) { return {}; }

  auto const stateFilePath = jobstate::buildDefaultStateFilePath(ctx.config);
  ctx.runtime.jobState = std::make_shared<jobstate::Store>(stateFilePath);
  auto discardedMismatched = false;
  auto const initRes =
    ctx.runtime.jobState
      ->initialize(ctx.config, ctx.config.restartState, &discardedMismatched);
  if (!initRes) { return eh::makeError("{}", initRes.error()); }
  ctx.runtime.jobStateMatched = initRes.value();

  if (discardedMismatched) {
    auto const message = std::format(
      "Saved job state does not match the current command; discarding it and starting "
      "a fresh run: {}",
      stateFilePath.string()
    );
    // Under -v the LOG_WARN echo is the console warning; avoid double printing.
    if (!ctx.config.verbose) { terminal::println(Warning, "{}", message); }
    LOG_WARN("{}", message);
  }

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

  // Remove the file but keep the store alive: the end-of-run summary reads
  // jobId/task counts from it after pipeline::run returns.
  auto ec = std::error_code{};
  fs::remove(ctx.runtime.jobState->stateFilePath(), ec);
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
  // --dry-run only has a plan for mp4 probing; any other pipeline would
  // actually produce output, so exit before any work happens.
  if (ctx.config.dryRun) {
    auto const plansMp4 = !ctx.config.packOnly
      && ctx.config.processType == "video"
      && ctx.config.outputFormat == "mp4"
      && !ctx.config.crf.has_value();
    if (!plansMp4) {
      terminal::println(
        Warning,
        "Dry run: no encoding plan for this pipeline; exiting without encoding."
      );
      return 0;
    }
  }

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
