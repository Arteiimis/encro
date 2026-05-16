#include "picture/picture_compress.h"

#include "core/display_text.h"
#include "core/progress.h"
#include "core/task_executor.h"
#include "infra/stop_signal.h"
#include "utils/utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace {

auto truncateForLabel(std::string const& text, std::size_t maxLen = 48) -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

struct BatchState {
  appctx::AppContext const& ctx;
  std::atomic_size_t& completed;
  std::vector<CompressResult>& results;
  std::mutex& resultsMutex;
  progress::ProgressContext& progressCtx;
};

auto shouldUseCompressedOutput(fs::path const& inputPath, fs::path const& outputPath)
  -> bool {
  auto ec = std::error_code{};
  auto const inputSize = fs::file_size(inputPath, ec);
  if (ec) {
    spdlog::warn(
      "Unable to read source image size, keeping compressed output: input={} error={}",
      inputPath.string(),
      ec.message()
    );
    return true;
  }

  ec.clear();
  auto const outputSize = fs::file_size(outputPath, ec);
  if (ec) {
    spdlog::warn(
      "Unable to read compressed image size, keeping compressed output: output={} "
      "error={}",
      outputPath.string(),
      ec.message()
    );
    return true;
  }

  return outputSize <= inputSize;
}

auto compressImageTask(
  CompressTask const& task,
  BatchState const& state,
  int quality,
  std::size_t total,
  std::size_t barIndex
) -> eh::Result<void> {
  if (stopsignal::isStopRequested()) {
    return eh::makeError("Compression canceled by user.");
  }

  auto const success = compressImage(state.ctx, task.inputPath, task.outputPath, quality);

  auto const done = state.completed.fetch_add(1, std::memory_order_release) + 1;

  if (success) {
    auto const usedCompressed =
      shouldUseCompressedOutput(task.inputPath, task.outputPath);
    if (!usedCompressed) {
      auto ec = std::error_code{};
      fs::remove(task.outputPath, ec);
      spdlog::info(
        "Skipping oversized JPEG output and keeping original file: input={} output={}",
        task.inputPath.string(),
        task.outputPath.string()
      );
    }

    auto lock = std::scoped_lock{state.resultsMutex};
    state.results.push_back({
      .originalPath = task.inputPath,
      .compressedPath = task.outputPath,
      .entryName = task.entryName,
      .originalEntryName = task.originalEntryName,
      .usedCompressed = usedCompressed,
    });
  }

  auto const percent = static_cast<float>(done) / static_cast<float>(total) * 100.0f;
  state.progressCtx.setProgress(barIndex, percent);
  state.progressCtx
    .setPostfixText(barIndex, std::format("Compressing: {}/{}", done, total));

  if (!success) {
    return eh::makeError("Failed to compress image: {}", task.inputPath.string());
  }

  return {};
}

}  // namespace

auto ImageCompressConfig::buildCMD() const -> std::string {
  auto cmd = std::string{ffmpegPath.value().string()};
  cmd += " -hide_banner -nostats -loglevel error -y";
  cmd += std::format(" -i \"{}\"", inputPath.string());
  cmd += std::format(" -q:v {}", quality);
  cmd += std::format(" \"{}\"", outputPath.string());
  return cmd;
}

auto compressImage(
  appctx::AppContext const& ctx,
  fs::path const& inputPath,
  fs::path const& outputPath,
  int quality
) -> bool {
  auto const cfg = ImageCompressConfig{
    .ffmpegPath = ctx.toolchain.ffmpegPath,
    .inputPath = inputPath,
    .outputPath = outputPath,
    .quality = quality,
  };

  auto const cmd = cfg.buildCMD();
  spdlog::debug("Compress image: {}", cmd);

  auto const [exitCode, output] = exec2(cmd);
  if (exitCode != 0) {
    spdlog::warn(
      "Image compression failed: input={} exitCode={}",
      inputPath.string(),
      exitCode
    );
    return false;
  }

  if (!fs::exists(outputPath)) {
    spdlog::warn(
      "Image compression produced no output: input={} expected={}",
      inputPath.string(),
      outputPath.string()
    );
    return false;
  }

  spdlog::debug(
    "Image compressed: {} -> {} ({} bytes)",
    inputPath.string(),
    outputPath.string(),
    fs::file_size(outputPath)
  );

  return true;
}

namespace {

auto probeMaxFileSize(std::span<CompressTask const> tasks) -> std::uintmax_t {
  auto maxSize = std::uintmax_t{0};
  for (auto const& task: tasks) {
    auto ec = std::error_code{};
    auto const size = fs::file_size(task.inputPath, ec);
    if (!ec && size > maxSize) { maxSize = size; }
  }
  return maxSize;
}

auto capConcurrencyByFileSize(std::uintmax_t maxFileSize, std::size_t maxParallel)
  -> std::size_t {
  constexpr auto kOneMB = 1024ULL * 1024;
  auto const result = maxParallel;
  if (maxFileSize > 20 * kOneMB) { return std::min(result, std::size_t{1}); }
  if (maxFileSize > 10 * kOneMB) { return std::min(result, std::size_t{3}); }
  if (maxFileSize > 5 * kOneMB) { return std::min(result, std::size_t{6}); }
  return result;
}

void retryFailedTasks(
  appctx::AppContext const& ctx,
  std::span<CompressTask const> allTasks,
  std::vector<CompressResult>& results,
  std::mutex& resultsMutex,
  int quality,
  progress::ProgressContext& progressCtx
) {
  auto failedTasks = std::vector<CompressTask>{};
  for (auto const& task: allTasks) {
    auto const found = std::ranges::any_of(results, [&](CompressResult const& r) {
      return r.originalPath == task.inputPath && r.entryName == task.entryName;
    });
    if (!found) { failedTasks.push_back(task); }
  }

  if (failedTasks.empty()) { return; }

  spdlog::info("Retrying {} failed compression(s) sequentially...", failedTasks.size());

  auto const retryBarIndex =
    progressCtx
      .addBar(std::format("Retrying: 0/{}", failedTasks.size()), progress::Tone::Active);

  auto recovered = std::size_t{0};
  for (std::size_t i = 0; i < failedTasks.size(); ++i) {
    if (stopsignal::isStopRequested()) { break; }

    auto const& task = failedTasks[i];
    auto const success = compressImage(ctx, task.inputPath, task.outputPath, quality);
    if (success) {
      auto const usedCompressed =
        shouldUseCompressedOutput(task.inputPath, task.outputPath);
      if (!usedCompressed) {
        auto ec = std::error_code{};
        fs::remove(task.outputPath, ec);
        spdlog::info(
          "Skipping oversized JPEG output and keeping original file: input={} output={}",
          task.inputPath.string(),
          task.outputPath.string()
        );
      }

      auto lock = std::scoped_lock{resultsMutex};
      results.push_back({
        .originalPath = task.inputPath,
        .compressedPath = task.outputPath,
        .entryName = task.entryName,
        .originalEntryName = task.originalEntryName,
        .usedCompressed = usedCompressed,
      });
      ++recovered;
    }

    auto const percent =
      static_cast<float>(i + 1) / static_cast<float>(failedTasks.size()) * 100.0f;
    progressCtx.setProgress(retryBarIndex, percent);
    progressCtx.setPostfixText(
      retryBarIndex,
      std::format("Retrying: {}/{}", i + 1, failedTasks.size())
    );
  }

  progressCtx.setTone(retryBarIndex, progress::Tone::Success);
  progressCtx.setPostfixText(
    retryBarIndex,
    std::format("Retried: {}/{}", recovered, failedTasks.size())
  );

  spdlog::info("Retry completed: {}/{} recovered", recovered, failedTasks.size());
}

}  // namespace

auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<CompressTask const> tasks,
  int quality,
  std::size_t maxParallel
) -> std::vector<CompressResult> {
  if (tasks.empty()) { return {}; }

  auto const maxFileSize = probeMaxFileSize(tasks);
  auto const effectiveMaxParallel = capConcurrencyByFileSize(maxFileSize, maxParallel);
  if (effectiveMaxParallel != maxParallel) {
    spdlog::info(
      "Adaptive concurrency: capped from {} to {} (max input file ~{} MB)",
      maxParallel,
      effectiveMaxParallel,
      maxFileSize / (1024 * 1024)
    );
  }

  auto progressCtx = progress::ProgressContext{};
  auto const total = tasks.size();
  auto completed = std::atomic_size_t{0};

  auto const barIndex =
    progressCtx.addBar(std::format("Compressing: 0/{}", total), progress::Tone::Active);

  auto results = std::vector<CompressResult>{};
  results.reserve(total);
  auto resultsMutex = std::mutex{};

  auto const state = BatchState{
    .ctx = ctx,
    .completed = completed,
    .results = results,
    .resultsMutex = resultsMutex,
    .progressCtx = progressCtx,
  };

  auto taskSpecs = std::vector<taskexec::TaskSpec>{};
  taskSpecs.reserve(total);
  for (auto const& task: tasks) {
    taskSpecs.push_back({
      .id = std::format("compress:{}", task.outputPath.string()),
      .label = task.inputPath.filename().string(),
      .run = [&state, &task, quality, total, barIndex](taskexec::TaskContext& _) {
        return compressImageTask(task, state, quality, total, barIndex);
      },
    });
  }

  auto const runState = taskexec::runTasks({
    .tasks = std::move(taskSpecs),
    .maxConcurrency = effectiveMaxParallel,
    .progress = &progressCtx,
    .hideCursor = true,
  });

  if (runState.canceled) {
    progressCtx.setTone(barIndex, progress::Tone::Failure);
    progressCtx
      .setPostfixText(barIndex, std::format("Canceled: {}/{}", results.size(), total));
    spdlog::info(
      "Image compression batch canceled: {}/{} succeeded before stop",
      results.size(),
      total
    );
    return results;
  }

  progressCtx.setTone(barIndex, progress::Tone::Success);
  progressCtx
    .setPostfixText(barIndex, std::format("Compressed: {}/{}", results.size(), total));

  auto const succeeded =
    std::ranges::count_if(runState.results, [](eh::Result<void> const& result) {
      return result.has_value();
    });
  spdlog::info("Image compression batch completed: {}/{} succeeded", succeeded, total);

  retryFailedTasks(ctx, tasks, results, resultsMutex, quality, progressCtx);

  auto const finalSucceeded = results.size();
  spdlog::info("Image compression final: {}/{} images compressed", finalSucceeded, total);

  return results;
}
