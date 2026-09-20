#include "pack/pack_service.h"
#include "pack/pack_internal.h"

#include "core/task_executor.h"
#include "core/collision_naming.h"

#include "infra/stop_signal.h"
#include "infra/terminal.h"

#include "logging/log_tags.h"
#include "logging/logging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace fs = std::filesystem;
using enum terminal::MessageKind;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::PACK_SERVICE);

namespace pack {
namespace {

auto formatCompactPackingStatus(
  std::size_t archiveIndex,
  std::size_t archiveCount,
  std::size_t fileIndex,
  std::size_t fileCount
) -> std::string;
auto formatCompactPackedStatus(std::size_t archiveIndex, std::size_t archiveCount)
  -> std::string;

struct PackTaskRecorder {
  PackPlan const& plan;
  std::vector<fs::path>& zippedFiles;

  void notifyGroupStart(std::size_t index) const {
    if (plan.progressCallbacks.onGroupStart) {
      plan.progressCallbacks.onGroupStart(index);
    }
  }

  auto fail(
    std::size_t index,
    fs::path const& zipPath,
    eh::Result<void> const& packRes
  ) const -> eh::Result<void> {
    if (plan.removeOnFailure) {
      auto ec = std::error_code{};
      fs::remove(zipPath, ec);
    }

    if (plan.progressCallbacks.onGroupFailure) {
      plan.progressCallbacks.onGroupFailure(index, packRes.error());
    }
    return eh::makeError("{}", packRes.error());
  }

  void succeed(std::size_t index, fs::path const& zipPath) const {
    zippedFiles[index] = zipPath;
    if (plan.progressCallbacks.onGroupSuccess) {
      plan.progressCallbacks.onGroupSuccess(index, zipPath);
    }
  }
};

using PackGroupTaskRunner = std::function<eh::Result<void>(
  std::size_t,
  fs::path const&,
  std::string_view,
  taskexec::TaskContext&,
  PackTaskRecorder&
)>;

struct CompactProgressState {
  progress::ProgressContext ctx;
  std::optional<std::size_t> barIndex;
  std::size_t completedFileCount = 0;
  std::atomic<std::size_t> completedArchiveCount{0};
  std::mutex mutex;
  std::atomic<std::size_t> finalizingCount{0};
  std::atomic<bool> spinnerStop{false};
  std::jthread spinnerThread;
  // Last packing status text; the head of every composed line.
  std::string label;

  void initBar(
    std::size_t archiveCount,
    std::size_t totalFiles,
    std::function<void(std::size_t, std::size_t)> const& onCompactProgress,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    auto const initialStatus = formatCompactPackingStatus(0, archiveCount, 0, totalFiles);
    barIndex = ctx.addBar(initialStatus, terminal::Role::Warn);
    ctx.setProgress(barIndex.value(), 0.0f);
    publish(initialStatus, onCompactStatusText);
    if (onCompactProgress) { onCompactProgress(0, totalFiles); }
  }

  // Single entry point for the compact line: the packing label is always the
  // head, and the finalizing indicator is appended as its own part while an
  // archive is writing its trailer. Both writers - packing updates and the
  // indicator - publish through here, so neither can erase the other's text.
  // An empty label keeps the current one (indicator repaints).
  void publish(
    std::string_view newLabel,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    auto lock = std::scoped_lock{mutex};
    if (!newLabel.empty()) { label = newLabel; }

    auto const text = composeText();
    if (barIndex.has_value()) { ctx.setPostfixText(barIndex.value(), text); }
    if (onCompactStatusText) { onCompactStatusText(text); }
  }

  // Wake immediately on a stop request; the manual-reset event stays signaled,
  // so fall back to the normal cadence to avoid a hot spin while the spinner's
  // own exit conditions catch up.
  void waitTick() {
    if (stopsignal::waitForStop(kFrameInterval)) {
      std::this_thread::sleep_for(kFrameInterval);
    }
  }

  void startSpinner(std::function<void(std::string_view)> const& onCompactStatusText) {
    // No destination for the indicator's output: bars cannot paint and no
    // status consumer is installed, so a timing thread would repaint nothing.
    if (!ctx.renderable() && !onCompactStatusText) { return; }

    spinnerThread = std::jthread{
      [this, &onCompactStatusText](
        std::stop_token
          stopToken  // NOLINT(performance-unnecessary-value-param): jthread callback signature is fixed
      ) {
        while (
          !stopToken.stop_requested() && !spinnerStop.load(std::memory_order_acquire)
        ) {
          if (finalizingCount.load(std::memory_order_acquire) > 0) {
            publish({}, onCompactStatusText);
          }
          // One repaint per frame interval, rendered or not: the wait is the
          // cadence, so the frame cannot advance at repaint speed.
          waitTick();
        }
      }
    };
  }

  void finish(
    std::size_t archiveCount,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    stopSpinner();
    if (barIndex.has_value()) {
      // Completion is terminal: every archive task has settled, so the
      // finalizing counter cannot legitimately still be up and the completion
      // text must not carry the indicator.
      finalizingCount.store(0, std::memory_order_release);
      auto const completedStatus = formatCompactPackedStatus(archiveCount, archiveCount);
      ctx.setRole(barIndex.value(), terminal::Role::Good);
      publish(completedStatus, onCompactStatusText);
    }
  }

  // The phase is over: stop the indicator clock and take the bar off screen.
  // Every exit path calls this, including the ones that never reach finish().
  void clearBars() {
    stopSpinner();
    ctx.eraseBars();
  }

private:
  void stopSpinner() {
    spinnerStop.store(true, std::memory_order_release);
    spinnerThread.request_stop();
    if (spinnerThread.joinable()) { spinnerThread.join(); }
  }
  // The indicator's four frames step once per interval, on the elapsed clock:
  // the clock owns the frame, so repainting more often cannot spin it faster.
  static constexpr auto kFrameInterval = std::chrono::milliseconds{120};
  static constexpr auto kFrames = std::array{'|', '/', '-', '\\'};

  static char currentFrame() {
    auto const frameTicks =
      std::chrono::steady_clock::now().time_since_epoch() / kFrameInterval;
    return kFrames[static_cast<std::size_t>(frameTicks) % kFrames.size()];
  }

  auto composeText() const -> std::string {
    if (finalizingCount.load(std::memory_order_acquire) == 0) { return label; }
    return std::format("{} | Finalizing {}", label, currentFrame());
  }
};

struct CompactPackRunner {
  Packer& packer;
  PackPlan const& plan;
  CompactProgressState& state;
  std::size_t totalFiles;
  std::size_t archiveCount;

  auto operator()(
    std::size_t index,
    fs::path const& zipPath,
    std::string_view /*label*/,
    taskexec::TaskContext& /*taskCtx*/,
    PackTaskRecorder& recorder
  ) const -> eh::Result<void> {
    auto const packRes = packer.packFilesToZip(
      plan.groups[index],
      zipPath,
      [this](std::size_t /*fileIndex*/, std::size_t /*fileCount*/) { onEntryPacked(); },
      &state.finalizingCount,
      plan.onBeforeArchiveClose
    );

    if (!packRes) { return recorder.fail(index, zipPath, packRes); }

    onGroupPacked();
    recorder.succeed(index, zipPath);
    return {};
  }

private:
  void onEntryPacked() const {
    auto statusText = std::string{};
    {
      auto lock = std::scoped_lock{state.mutex};
      ++state.completedFileCount;

      auto const percent = totalFiles == 0 ? 100.0f
                                           : static_cast<float>(state.completedFileCount)
          / static_cast<float>(totalFiles)
          * 100.0f;
      statusText = formatCompactPackingStatus(
        state.completedArchiveCount.load(std::memory_order_acquire),
        archiveCount,
        state.completedFileCount,
        totalFiles
      );

      if (state.barIndex.has_value()) {
        state.ctx.setProgress(state.barIndex.value(), percent);
      }
      if (plan.progressCallbacks.onCompactProgress) {
        plan.progressCallbacks.onCompactProgress(state.completedFileCount, totalFiles);
      }
    }
    state.publish(statusText, plan.progressCallbacks.onCompactStatusText);
  }

  void onGroupPacked() const {
    auto const completed = state.completedArchiveCount.fetch_add(1) + 1;
    auto statusText = std::string{};
    {
      auto lock = std::scoped_lock{state.mutex};
      statusText = formatCompactPackingStatus(
        completed,
        archiveCount,
        state.completedFileCount,
        totalFiles
      );
    }
    state.publish(statusText, plan.progressCallbacks.onCompactStatusText);
  }
};

auto runPackTaskPlan(
  PackPlan const& plan,
  PackGroupTaskRunner const& runGroup,
  progress::ProgressContext* progressCtx = nullptr
) -> eh::Result<std::vector<fs::path>> {
  auto const maxParallelJobs =
    std::max<std::size_t>(1, plan.maxParallelJobs.value_or(plan.groups.size()));
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto recorder = PackTaskRecorder{plan, zippedFiles};
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(plan.groups.size());

  for (auto index = std::size_t{0}; index < plan.groups.size(); ++index) {
    auto const zipName = internal::resolveZipNameForIndex(plan, index);
    auto const zipPath = plan.outputDir / zipName;
    auto const label = internal::defaultProgressLabelForZipName(zipName);

    tasks.push_back({
      .id = std::format("archive:{}", collisionnaming::stablePathString(zipPath)),
      .label = label,
      .input = zipPath.string(),
      .run = [&, index, zipPath, label](  // NOLINT(bugprone-exception-escape): taskexec::runTasks catches
               taskexec::TaskContext& taskCtx
             ) {
        recorder.notifyGroupStart(index);
        return runGroup(index, zipPath, label, taskCtx, recorder);
      },
    });
  }

  auto const runRes = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = maxParallelJobs,
    .progress = progressCtx,
    .hideCursor = true,
  });

  if (runRes.canceled && runRes.skippedCount() > 0) {
    return eh::makeError("Packing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < runRes.outcomes.size(); ++index) {
    auto const& outcome = runRes.outcomes[index];
    if (outcome.state == taskexec::TaskState::Skipped) { continue; }
    if (outcome.state == taskexec::TaskState::Failed) {
      return eh::makeError("{}", outcome.error);
    }
  }

  return zippedFiles;
}

std::size_t countPackedFiles(std::vector<std::vector<PackFileEntry>> const& groups) {
  auto total = std::size_t{0};
  for (auto const& group: groups) { total += group.size(); }
  return total;
}

auto formatCompactPackingStatus(
  std::size_t archiveIndex,
  std::size_t archiveCount,
  std::size_t fileIndex,
  std::size_t fileCount
) -> std::string {
  return std::format(
    "Packing: archive {}/{} [file {}/{}]",
    archiveIndex,
    archiveCount,
    fileIndex,
    fileCount
  );
}

auto formatCompactPackedStatus(std::size_t archiveIndex, std::size_t archiveCount)
  -> std::string {
  return std::format("Packed: archive {}/{} complete", archiveIndex, archiveCount);
}

}  // namespace

auto PackService::packGroupsCompact(PackPlan const& plan)
  -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }
  fs::create_directories(plan.outputDir);

  auto state = CompactProgressState{};
  auto const totalFiles = countPackedFiles(plan.groups);
  auto const archiveCount = plan.groups.size();

  state.initBar(
    archiveCount,
    totalFiles,
    plan.progressCallbacks.onCompactProgress,
    plan.progressCallbacks.onCompactStatusText
  );
  state.startSpinner(plan.progressCallbacks.onCompactStatusText);
  auto const runRes = runPackTaskPlan(
    plan,
    CompactPackRunner{
      .packer = packer_,
      .plan = plan,
      .state = state,
      .totalFiles = totalFiles,
      .archiveCount = archiveCount,
    }
  );
  if (runRes) { state.finish(archiveCount, plan.progressCallbacks.onCompactStatusText); }
  // Clearing before the error branch: a failed or canceled pack leaves no bar
  // on screen either.
  state.clearBars();
  if (!runRes) { return eh::makeError("{}", runRes.error()); }

  return runRes.value();
}

auto PackService::packGroupsFull(PackPlan const& plan)
  -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }
  fs::create_directories(plan.outputDir);

  // The phase owns the bars its archives render (packer adds one per archive
  // to this context), so it is the one that clears them when the plan ends.
  auto progressCtx = progress::ProgressContext{};
  auto const runRes = runPackTaskPlan(
    plan,
    [this, &plan](
      std::size_t index,
      fs::path const& zipPath,
      std::string_view label,
      taskexec::TaskContext& taskCtx,
      PackTaskRecorder& recorder
    ) -> eh::Result<void> {
      auto const packRes =
        packer_.packFilesToZip(plan.groups[index], zipPath, taskCtx.progress, label);
      if (!packRes) { return recorder.fail(index, zipPath, packRes); }

      recorder.succeed(index, zipPath);
      return {};
    },
    &progressCtx
  );
  progressCtx.eraseBars();
  return runRes;
}

namespace internal {

auto appendOrdinalRangeSuffix(std::string_view fileName, FileOrdinalRange const& range)
  -> std::string {
  if (range.first == 0 || range.last == 0 || range.count == 0) {
    return std::string{fileName};
  }

  auto const filePath = fs::path{fileName};
  auto const suffix = std::format("[{}~{}#{}p]", range.first, range.last, range.count);
  return std::format(
    "{}{}{}",
    filePath.stem().string(),
    suffix,
    filePath.extension().string()
  );
}

auto defaultZipNameForIndex(std::size_t index) -> std::string {
  return std::format("part{}.zip", index + 1);
}

auto defaultProgressLabelForZipName(std::string_view zipName) -> std::string {
  return std::format("Packing: {}", zipName);
}

auto resolveZipNameForIndex(PackPlan const& plan, std::size_t index) -> std::string {
  return plan.zipNameForIndex ? plan.zipNameForIndex(index)
                              : defaultZipNameForIndex(index);
}

auto makeSubsetZipNameResolver(
  std::function<std::string(std::size_t)> const& originalResolver,
  std::shared_ptr<std::vector<std::size_t>> const& selectedIndexes
) -> std::function<std::string(std::size_t)> {
  return [originalResolver, selectedIndexes](std::size_t subsetIndex) -> std::string {
    auto const actualIndex = selectedIndexes->at(subsetIndex);
    return originalResolver ? originalResolver(actualIndex)
                            : defaultZipNameForIndex(actualIndex);
  };
}

auto selectPackPlanIndexes(PackPlan const& plan, std::span<std::size_t const> indexes)
  -> PackPlan {
  auto filteredGroups = std::vector<std::vector<PackFileEntry>>{};
  filteredGroups.reserve(indexes.size());
  for (auto const index: indexes) { filteredGroups.push_back(plan.groups[index]); }

  auto const selectedIndexes =
    std::make_shared<std::vector<std::size_t>>(indexes.begin(), indexes.end());

  return PackPlan{
    .groups = std::move(filteredGroups),
    .outputDir = plan.outputDir,
    .zipNameForIndex = makeSubsetZipNameResolver(plan.zipNameForIndex, selectedIndexes),
    .progressCallbacks =
      {
        .onCompactProgress = plan.progressCallbacks.onCompactProgress,
        .onCompactStatusText = plan.progressCallbacks.onCompactStatusText,
      },
    .onBeforeArchiveClose = plan.onBeforeArchiveClose,
    .maxParallelJobs = plan.maxParallelJobs,
    .removeOnFailure = plan.removeOnFailure,
    .compact = plan.compact,
  };
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): impl is owned by the returned PackPlan

}  // namespace internal

auto PackService::packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  logging::ScopedTimer timer("pack.execute");
  auto const packLabel = std::format("{} group(s)", plan.groups.size());
  logging::ScopedErrorContext ctx("pack.execute", packLabel);
  if (plan.compact) { return packGroupsCompact(plan); }
  return packGroupsFull(plan);
}

}  // namespace pack
