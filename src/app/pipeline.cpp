#include "app/pipeline.h"

#include "core/job_state.h"
#include "infra/terminal.h"
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

auto runPackOnly(appctx::AppContext& ctx) -> eh::Result<int> {
  if (ctx.config.processType != "video") {
    return eh::makeError("pack-only option is only supported when --type is video.");
  }

  if (!fs::is_directory(ctx.config.inputPath)) {
    return eh::makeError("pack-only mode requires input to be a directory.");
  }

  return runDirectoryPackWorkflow(ctx, ctx.config.inputPath);
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

  if (ctx.config.packOnly) { return runPackOnly(ctx); }

  if (ctx.config.processType == "video") { return runVideo(ctx); }

  if (ctx.config.processType == "picture") { return runPicture(ctx); }

  return eh::makeError(
    "Invalid process type: {}. Valid types are: video, picture.",
    ctx.config.processType
  );
}

}  // namespace pipeline
