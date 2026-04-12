#include "core/pipeline.h"

#include "pack/packer.h"
#include "pack/picture_process.h"
#include "video/video_process.h"

#include <filesystem>
#include <print>

namespace fs = std::filesystem;

namespace pipeline {

namespace {

auto runPackOnly(appctx::AppContext& ctx) -> eh::Result<int> {
  if (ctx.config.processType != "video") {
    return eh::makeError("pack-only option is only supported when --type is video.");
  }

  if (!fs::is_directory(ctx.config.inputPath)) {
    return eh::makeError("pack-only mode requires input to be a directory.");
  }

  auto const zipOutputDir =
    ctx.config.outputPath.value_or(ctx.config.inputPath / "packed");
  auto const packRes = packAllFilesInDirectory(
    ctx.config.inputPath,
    zipOutputDir,
    500 * 1024 * 1024,
    true,
    ctx.config.forceNameConflictHandling,
    ctx.config.maxParallelJobs
  );

  if (!packRes) {
    return eh::makeError("Failed to pack files: {}", packRes.error());
  }

  std::println("All files packed successfully to: {}", zipOutputDir.string());
  return 0;
}

auto runVideo(appctx::AppContext& ctx) -> eh::Result<int> {
  if (!ctx.config.inputPaths.empty()) {
    return handleMultiFileEncoding(ctx, ctx.config.inputPaths);
  }

  auto const& inputPath = ctx.config.inputPath;

  if (fs::is_directory(inputPath)) { return handlePathEncoding(ctx, inputPath); }

  if (fs::is_regular_file(inputPath)) {
    return handleSingleFileEncoding(ctx, inputPath);
  }

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

  auto const outputDir =
    ctx.config.outputPath.value_or(ctx.config.inputPath) / "packed";
  auto const packRes =
    packAllPicsToZipParallel(ctx.config, ctx.config.inputPath, outputDir);

  if (!packRes) {
    return eh::makeError("Failed to pack pictures: {}", packRes.error());
  }

  std::println("All pictures packed successfully to: {}", outputDir.string());
  return 0;
}

}  // namespace

auto run(appctx::AppContext& ctx) -> eh::Result<int> {
  if (ctx.config.packOnly) { return runPackOnly(ctx); }

  if (ctx.config.processType == "video") { return runVideo(ctx); }

  if (ctx.config.processType == "picture") { return runPicture(ctx); }

  return eh::makeError(
    "Invalid process type: {}. Valid types are: video, picture.",
    ctx.config.processType
  );
}

}  // namespace pipeline
