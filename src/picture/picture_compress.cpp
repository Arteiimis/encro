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
    auto lock = std::scoped_lock{state.resultsMutex};
    state.results.push_back(
      CompressResult{
        .originalPath = task.inputPath,
        .compressedPath = task.outputPath,
        .entryName = task.entryName,
      }
    );
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

auto compressImageBatch(
  appctx::AppContext& ctx,
  std::span<CompressTask const> tasks,
  int quality,
  std::size_t maxParallel
) -> std::vector<CompressResult> {
  if (tasks.empty()) { return {}; }

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
    taskSpecs.push_back(
      taskexec::TaskSpec{
        .id = std::format("compress:{}", task.outputPath.string()),
        .label = task.inputPath.filename().string(),
        .run = [&state, &task, quality, total, barIndex](taskexec::TaskContext& _)
          -> eh::Result<void> {
          return compressImageTask(task, state, quality, total, barIndex);
        }
      }
    );
  }

  auto const runState = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(taskSpecs),
      .maxConcurrency = maxParallel,
      .progress = &progressCtx,
      .hideCursor = true,
    }
  );

  progressCtx.setTone(barIndex, progress::Tone::Success);
  progressCtx
    .setPostfixText(barIndex, std::format("Compressed: {}/{}", results.size(), total));

  auto const succeeded =
    std::ranges::count_if(runState.results, [](eh::Result<void> const& result) {
      return result.has_value();
    });
  spdlog::info("Image compression batch completed: {}/{} succeeded", succeeded, total);

  return results;
}
