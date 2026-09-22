#include "picture/picture_compress.h"

#include "core/display_text.h"
#include "core/media_item.h"
#include "core/progress.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "utils/utils.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <format>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PICTURE_COMPRESS);

namespace fs = std::filesystem;

namespace {

auto truncateForLabel(std::string const& text, std::size_t maxLen = 48) -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

}  // namespace

auto ImageCompressConfig::buildCMD() const -> std::string {
  auto cmd = quoteToolPath(ffmpegPath.value_or(fs::path{"ffmpeg"}));
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -i \"{}\"", inputPath.string());
  cmd += std::format(" -q:v {}", quality);
  cmd += std::format(" \"{}\"", outputPath.string());
  return cmd;
}

auto partialTempPath(fs::path const& outputPath) -> fs::path {
  return outputPath.parent_path()
    / (outputPath.stem().string() + ".partial" + outputPath.extension().string());
}

bool compressImage(
  appctx::AppContext const& ctx,
  fs::path const& inputPath,
  fs::path const& outputPath,
  int quality,
  std::string* failureReason
) {
  auto const partialPath = partialTempPath(outputPath);

  auto const cfg = ImageCompressConfig{
    .ffmpegPath = ctx.toolchain.ffmpegPath,
    .inputPath = inputPath,
    .outputPath = partialPath,
    .quality = quality,
  };

  auto const cmd = cfg.buildCMD();
  LOG_DEBUG("Compress image: {}", cmd);

  auto const [exitCode, output, pid, stderrText] = exec2(cmd);
  if (exitCode != 0) {
    auto const reason = extractFailureReason(output, "", exitCode);
    LOG_WARN(
      "Image compression failed: input={} exitCode={} reason={}",
      inputPath.string(),
      exitCode,
      reason
    );
    if (failureReason != nullptr) { *failureReason = reason; }
    return false;
  }

  if (!fs::exists(partialPath)) {
    LOG_WARN(
      "Image compression produced no output: input={} expected={}",
      inputPath.string(),
      partialPath.string()
    );
    return false;
  }

  auto ec = std::error_code{};
  fs::remove(outputPath, ec);
  fs::rename(partialPath, outputPath, ec);
  if (ec) {
    LOG_WARN(
      "Image compression output rename failed: input={} output={} error={}",
      inputPath.string(),
      outputPath.string(),
      ec.message()
    );
    return false;
  }

  LOG_DEBUG(
    "Image compressed: {} -> {} ({} bytes)",
    inputPath.string(),
    outputPath.string(),
    fs::file_size(outputPath)
  );

  return true;
}

namespace {

std::uintmax_t probeMaxFileSize(std::span<MediaItem const> items) {
  auto maxSize = std::uintmax_t{0};
  for (auto const& item: items) {
    auto ec = std::error_code{};
    auto const size = fs::file_size(item.sourcePath, ec);
    if (!ec && size > maxSize) { maxSize = size; }
  }
  return maxSize;
}

std::size_t
capConcurrencyByFileSize(std::uintmax_t maxFileSize, std::size_t maxParallel) {
  constexpr auto kOneMB = 1024ULL * 1024;
  auto const result = maxParallel;
  if (maxFileSize > 20 * kOneMB) { return std::min(result, std::size_t{1}); }
  if (maxFileSize > 10 * kOneMB) { return std::min(result, std::size_t{3}); }
  if (maxFileSize > 5 * kOneMB) { return std::min(result, std::size_t{6}); }
  return result;
}

void retryFailedTasks(
  appctx::AppContext const& ctx,
  std::span<MediaItem> items,
  int quality,
  progress::ProgressContext& progressCtx
) {
  auto failedItems = std::vector<MediaItem*>{};
  for (auto& item: items) {
    if (item.outcome().state != mediaitem::ItemState::Succeeded) {
      failedItems.push_back(&item);
    }
  }

  if (failedItems.empty()) { return; }

  LOG_INFO("Retrying {} failed compression(s) sequentially...", failedItems.size());

  auto const retryBarIndex =
    progressCtx
      .addBar(std::format("Retrying: 0/{}", failedItems.size()), terminal::Role::Accent);

  auto recovered = std::size_t{0};
  for (std::size_t i = 0; i < failedItems.size(); ++i) {
    if (stopsignal::isStopRequested()) { break; }

    auto& item = *failedItems[i];
    std::string failureReason;
    if (compressImage(ctx, item.sourcePath, item.outputPath, quality, &failureReason)) {
      // The retry pass runs after the stage, so its write-back is its own.
      item.outcome().state = mediaitem::ItemState::Succeeded;
      ++recovered;
    } else if (!failureReason.empty() && item.outcome().failureReason.empty()) {
      // The first reason wins, as the flows' failure maps did.
      item.outcome().failureReason = failureReason;
    }

    auto const percent =
      static_cast<float>(i + 1) / static_cast<float>(failedItems.size()) * 100.0f;
    progressCtx.setProgress(retryBarIndex, percent);
    progressCtx.setPostfixText(
      retryBarIndex,
      std::format("Retrying: {}/{}", i + 1, failedItems.size())
    );
  }

  progressCtx.setRole(retryBarIndex, terminal::Role::Good);
  progressCtx.setPostfixText(
    retryBarIndex,
    std::format("Retried: {}/{}", recovered, failedItems.size())
  );

  LOG_INFO("Retry completed: {}/{} recovered", recovered, failedItems.size());
}

}  // namespace

// NOLINTNEXTLINE(readability-function-size): linear orchestration; the retry helper is extracted
auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<MediaItem> items,
  int quality,
  std::size_t maxParallel
) -> mediaitem::StageResult {
  if (items.empty()) { return {}; }

  auto const maxFileSize = probeMaxFileSize(items);
  auto const effectiveMaxParallel = capConcurrencyByFileSize(maxFileSize, maxParallel);
  if (effectiveMaxParallel != maxParallel) {
    LOG_INFO(
      "Adaptive concurrency: capped from {} to {} (max input file ~{} MB)",
      maxParallel,
      effectiveMaxParallel,
      maxFileSize / 1024 / 1024
    );
  }

  auto progressCtx = progress::ProgressContext{};
  auto const total = items.size();

  auto const barIndex =
    progressCtx.addBar(std::format("Compressing: 0/{}", total), terminal::Role::Accent);

  auto const result = mediaitem::runStage(
    mediaitem::StageSpec{
      .progress = &progressCtx,
      .barIndex = barIndex,
      .verb = "Compressing",
      .maxConcurrency = effectiveMaxParallel,
    },
    items,
    [](MediaItem const&) { return false; },
    [&ctx, quality](MediaItem& item, taskexec::TaskContext&) -> eh::Result<void> {
      if (stopsignal::isStopRequested()) {
        // A task that starts after the stop fails with no reason to print: the
        // flow's cancel path reports the stop itself.
        return eh::makeError("");
      }
      std::string failureReason;
      if (compressImage(ctx, item.sourcePath, item.outputPath, quality, &failureReason)) {
        return {};
      }
      return eh::makeError("{}", failureReason);
    }
  );

  if (result.canceled) {
    mediaitem::closeCanceledStage(progressCtx, barIndex, result.succeeded, total);
    LOG_INFO(
      "Image compression batch canceled: {}/{} succeeded before stop",
      result.succeeded,
      total
    );
    return result;
  }

  progressCtx.setRole(barIndex, terminal::Role::Good);
  progressCtx
    .setPostfixText(barIndex, std::format("Compressed: {}/{}", result.succeeded, total));
  LOG_INFO("Image compression batch completed: {}/{} succeeded", result.succeeded, total);

  retryFailedTasks(ctx, items, quality, progressCtx);

  auto const finalSucceeded = std::ranges::count_if(items, [](MediaItem const& item) {
    return item.result.state == mediaitem::ItemState::Succeeded;
  });
  LOG_INFO("Image compression final: {}/{} images compressed", finalSucceeded, total);

  progressCtx.eraseBars();
  return result;
}
