#include "video/video_batch_execution.h"

#include "video/video_info.h"
#include "video/video_progress_parser.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"

#include "logging/log_tags.h"
#include "logging/logging.h"
#include "logging/setup.h"

#include <chrono>
#include <thread>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_STATE);

namespace fs = std::filesystem;
using videoworkflow::maybeJobState;
using videoworkflow::withJobState;

namespace {

constexpr auto kProgressParseInterval = std::chrono::milliseconds{250};
constexpr auto kScrollTickInterval = std::chrono::milliseconds{100};

void noteStopRequest(appctx::AppContext& ctx) {
  if (!stopsignal::isStopRequested()) { return; }
  withJobState(ctx, [](jobstate::Store& store) { store.requestCancel(); });
}

// Stat-skip: reports whether the progress file changed since the last parse
// pass, so the monitor does not re-read an untouched file. Keyed by path:
// a segment switch swaps state.progressFilePath, which must trigger a read
// even if the new file happens to match the previous size.
auto progressFileChanged(appctx::EncodingState& state) -> bool {
  auto progressFilePath = std::optional<fs::path>{};
  {
    auto lock = std::scoped_lock{state.mtx};
    progressFilePath = state.progressFilePath;
  }
  if (!progressFilePath.has_value()) { return false; }

  auto ec = std::error_code{};
  auto const fileSize = fs::file_size(progressFilePath.value(), ec);

  auto lock = std::scoped_lock{state.mtx};
  if (ec) {
    // File removed (e.g. WebP steps clear and recreate the same progress
    // path, or a fresh segment has not been created yet). Reset the recorded
    // size to zero so the recreated file is detected once it grows past zero,
    // even if it later reaches the previously recorded size.
    state.lastProgressPath = progressFilePath;
    state.lastProgressFileSize = 0;
    return false;
  }
  if (
    state.lastProgressPath == progressFilePath && state.lastProgressFileSize == fileSize
  ) {
    return false;
  }
  state.lastProgressPath = progressFilePath;
  state.lastProgressFileSize = fileSize;
  return true;
}

auto getStateLabel(appctx::EncodingState const& state) -> std::string {
  return displaytext::pathToUtf8String(state.inputPath.filename());
}

auto tryReadProgressData(fs::path const& progressFilePath)
  -> std::optional<ProgressData> {
  auto const data = parseProgressFile(progressFilePath);
  if (!data.has_value()) {
    // A non-empty progress file that cannot be parsed means ffmpeg's output
    // format changed — warn instead of degrading silently. Empty/missing
    // files are the normal pre-first-write state and stay quiet.
    auto ec = std::error_code{};
    auto const size = fs::file_size(progressFilePath, ec);
    if (!ec && size > 0) {
      LOG_WARN(
        "Failed to parse ffmpeg progress file ({} bytes): {}",
        size,
        progressFilePath.string()
      );
    }
  }
  return data;
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

  return progressPercent(
    progressData->frameCount,
    state.baseFrameOffset,
    state.totalFrames.value()
  );
}

auto stateFinished(appctx::EncodingState& state) -> bool {
  auto lock = std::scoped_lock{state.mtx};
  return state.finished;
}

void renderStalled(
  videobatch::detail::EncodingExecutionContext& executionCtx,
  appctx::EncodingState& activeState
) {
  auto barIndex = std::optional<std::size_t>{};
  auto lastError = std::optional<std::string>{};
  auto lastStatus = std::optional<std::string>{};
  auto actionId = std::optional<std::string>{};
  {
    auto lock = std::scoped_lock{activeState.mtx};
    barIndex = activeState.barIndex;
    lastError = activeState.lastError;
    lastStatus = activeState.lastStatus;
    actionId = activeState.actionId;
  }

  if (barIndex.has_value()) {
    auto const fileLabel = getStateLabel(activeState);
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
    if (auto* store = maybeJobState(executionCtx.app); actionId.has_value()) {
      store
        ->markProgress(actionId.value(), std::nullopt, std::nullopt, lastStatus.value());
    }
  }
}

void renderProgress(
  videobatch::detail::EncodingExecutionContext& executionCtx,
  appctx::EncodingState& activeState,
  float progressValue
) {
  auto barIndex = std::optional<std::size_t>{};
  auto actionId = std::optional<std::string>{};
  auto lastFrameCount = std::optional<std::uint64_t>{};
  {
    auto lock = std::scoped_lock{activeState.mtx};
    activeState.lastProgress = progressValue;
    activeState.lastProgressAtomic.store(progressValue, std::memory_order_release);
    barIndex = activeState.barIndex;
    actionId = activeState.actionId;
    lastFrameCount = activeState.lastFrameCount;
  }

  if (barIndex.has_value()) {
    executionCtx.progress().setTone(barIndex.value(), progress::Tone::Active);
    executionCtx.progress().setProgress(barIndex.value(), progressValue);
  }

  if (auto* store = maybeJobState(executionCtx.app); actionId.has_value()) {
    store->markProgress(actionId.value(), progressValue, lastFrameCount, std::nullopt);
  }
}

// One throttled parse pass: reads and renders every active state whose
// progress file changed. Called at most kProgressParseInterval apart.
auto runParsePass(videobatch::detail::EncodingExecutionContext& executionCtx) -> void {
  auto const activeStates = executionCtx.activeStates();

  for (auto const& activeState: activeStates) {
    if (!activeState || stateFinished(*activeState)) { continue; }
    if (!progressFileChanged(*activeState)) { continue; }

    auto const progress = getEncodingProgress(executionCtx.app, *activeState);
    if (!progress.has_value()) {
      renderStalled(executionCtx, *activeState);
      continue;
    }

    renderProgress(executionCtx, *activeState, progress.value());
  }

  executionCtx.updateOverall();
}

void monitorEncodingProgress(videobatch::detail::EncodingExecutionContext& executionCtx) {
  using namespace std::chrono_literals;

  // First pass runs immediately, later passes every kProgressParseInterval.
  auto lastParseAt = std::chrono::steady_clock::now() - kProgressParseInterval;
  auto lastTickAt = std::chrono::steady_clock::now();

  while (true) {
    noteStopRequest(executionCtx.app);
    auto const activeStates = executionCtx.activeStates();

    if (executionCtx.finished() >= executionCtx.overallTotal()) { break; }

    if (stopsignal::isStopRequested() && activeStates.empty()) {
      LOG_INFO("Encoding monitor exiting after stop request; no active tasks remain.");
      break;
    }

    // Progress parsing and bar rendering run on the throttled cadence; the
    // loop still wakes every 20 ms for stop detection and the forensic
    // snapshot.
    auto const now = std::chrono::steady_clock::now();
    if (now - lastParseAt >= kProgressParseInterval) {
      lastParseAt = now;
      runParsePass(executionCtx);
    }

    // Scroll animation is repainted on its own timer, independent of
    // progress-file updates, so long labels keep scrolling while a file's
    // progress is unchanged.
    if (now - lastTickAt >= kScrollTickInterval) {
      lastTickAt = now;
      executionCtx.progress().tick();
    }

    logging::updateForensicSnapshot(
      static_cast<int>(activeStates.size()),
      static_cast<int>(executionCtx.counters().workers),
      static_cast<int>(executionCtx.pendingTotal()),
      static_cast<int>(executionCtx.finished())
    );

    // Wake immediately on stop; the manual-reset event stays signaled, so
    // fall back to the normal cadence during the drain phase (tasks still
    // finalizing) to avoid a hot spin.
    if (stopsignal::waitForStop(std::chrono::milliseconds{20})) {
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
  }

  executionCtx.updateOverall();
}

}  // namespace

auto videobatch::detail::startEncodingMonitor(
  videobatch::detail::EncodingExecutionContext& executionCtx
) -> std::jthread {
  return std::jthread([&executionCtx] { monitorEncodingProgress(executionCtx); });
}
