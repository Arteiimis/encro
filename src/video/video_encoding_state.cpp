#include "video/video_batch_execution.h"

#include "video/video_info.h"
#include "video/video_progress_parser.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

namespace fs = std::filesystem;
using videoworkflow::withActionJobState;
using videoworkflow::withJobState;

namespace {

void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](jobstate::Store& store) { store.requestCancel(); });
}

auto truncateForProgressLabel(std::string const& text, std::size_t maxLen = 48)
  -> std::string {
  return displaytext::truncateWithEllipsis(text, maxLen);
}

auto getStateLabel(appctx::EncodingState const& state) -> std::string {
  return truncateForProgressLabel(
    displaytext::pathToUtf8String(state.inputPath.filename())
  );
}

auto tryReadProgressData(fs::path const& progressFilePath)
  -> std::optional<ProgressData> {
  return parseProgressFile(progressFilePath);
}

auto getEncodingProgress(appctx::AppContext& ctx, appctx::EncodingState& state)
  -> std::optional<float> {
  if (!state.totalFrames.has_value()) {
    auto const totalFramesRes =
      getVidTotalFrames(ctx.toolchain, ctx.runtime, state.inputPath);
    if (!totalFramesRes.has_value()) {
      auto lock = std::scoped_lock{state.mtx};
      state.lastError = totalFramesRes.error();
      return std::nullopt;
    }
    state.totalFrames = totalFramesRes.value();
  }

  auto progressFilePath = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    progressFilePath = state.progressFilePath;
  }
  if (!progressFilePath.has_value()) { return std::nullopt; }

  auto const progressData = tryReadProgressData(progressFilePath.value());
  if (!progressData.has_value()) { return std::nullopt; }

  {
    auto lock = std::scoped_lock{state.mtx};
    state.lastFrameCount = progressData->frameCount;
  }

  return (static_cast<float>(progressData->frameCount) / state.totalFrames.value())
    * 100.0f;
}

void monitorEncodingProgress(videobatch::detail::EncodingExecutionContext& executionCtx) {
  using namespace std::chrono_literals;

  while (true) {
    noteStopRequest(executionCtx.app);
    auto const activeStates = executionCtx.activeStates();

    if (executionCtx.finished() >= executionCtx.overallTotal()) { break; }

    if (stopsignal::isStopRequested() && activeStates.empty()) {
      spdlog::info(
        "Encoding monitor exiting after stop request; no active tasks remain."
      );
      break;
    }

    for (auto const& activeState: activeStates) {
      if (!activeState) { continue; }

      auto const progress = getEncodingProgress(executionCtx.app, *activeState);
      if (!progress.has_value()) {
        auto barIndex = std::optional<std::size_t>{};
        auto lastError = std::optional<std::string>{};
        auto lastStatus = std::optional<std::string>{};
        auto actionId = std::optional<std::string>{};
        {
          auto lock = std::scoped_lock{activeState->mtx};
          barIndex = activeState->barIndex;
          lastError = activeState->lastError;
          lastStatus = activeState->lastStatus;
          actionId = activeState->actionId;
        }

        if (barIndex.has_value()) {
          auto const fileLabel = getStateLabel(*activeState);
          if (lastError.has_value()) {
            executionCtx.progress().setTone(barIndex.value(), progress::Tone::Failure);
            executionCtx.progress().setPostfixText(
              barIndex.value(),
              std::format("Encoding: {} | {}", fileLabel, lastError.value())
            );
          } else if (lastStatus.has_value()) {
            executionCtx.progress().setTone(barIndex.value(), progress::Tone::Active);
            executionCtx.progress().setPostfixText(
              barIndex.value(),
              std::format("Encoding: {} | {}", fileLabel, lastStatus.value())
            );
          }
        }

        if (lastStatus.has_value()) {
          withActionJobState(
            executionCtx.app,
            actionId,
            [&](jobstate::Store& currentStore, std::string const& currentActionId) {
              currentStore.markProgress(
                currentActionId,
                std::nullopt,
                std::nullopt,
                lastStatus.value()
              );
            }
          );
        }

        continue;
      }

      auto barIndex = std::optional<std::size_t>{};
      auto actionId = std::optional<std::string>{};
      auto lastFrameCount = std::optional<std::uint64_t>{};
      {
        auto lock = std::scoped_lock{activeState->mtx};
        activeState->lastProgress = progress.value();
        activeState->lastProgressAtomic
          .store(progress.value(), std::memory_order_release);
        barIndex = activeState->barIndex;
        actionId = activeState->actionId;
        lastFrameCount = activeState->lastFrameCount;
      }

      if (barIndex.has_value()) {
        executionCtx.progress().setTone(barIndex.value(), progress::Tone::Active);
        executionCtx.progress().setProgress(barIndex.value(), progress.value());
      }

      withActionJobState(
        executionCtx.app,
        actionId,
        [&](jobstate::Store& currentStore, std::string const& currentActionId) {
          currentStore.markProgress(
            currentActionId,
            progress.value(),
            lastFrameCount,
            std::nullopt
          );
        }
      );
    }

    executionCtx.updateOverall();

    std::this_thread::sleep_for(20ms);
  }

  executionCtx.updateOverall();
}

}  // namespace

auto videobatch::detail::startEncodingMonitor(
  videobatch::detail::EncodingExecutionContext& executionCtx
) -> std::jthread {
  return std::jthread([&executionCtx] { monitorEncodingProgress(executionCtx); });
}
