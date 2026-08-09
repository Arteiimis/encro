#include "pack/pack_service.h"
#include "pack/pack_internal.h"

#include "core/job_state.h"
#include "core/task_executor.h"
#include "core/collision_naming.h"

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
  std::vector<eh::Result<void>>& packResults;
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

    packResults[index] = packRes;
    if (plan.progressCallbacks.onGroupFailure) {
      plan.progressCallbacks.onGroupFailure(index, packRes.error());
    }
    return eh::makeError("{}", packRes.error());
  }

  void succeed(std::size_t index, fs::path const& zipPath) const {
    packResults[index] = {};
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

  void initBar(
    std::size_t archiveCount,
    std::size_t totalFiles,
    std::function<void(std::size_t, std::size_t)> const& onCompactProgress,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    auto const initialStatus = formatCompactPackingStatus(0, archiveCount, 0, totalFiles);
    barIndex = ctx.addBar(initialStatus, progress::Tone::Packing);
    ctx.setProgress(barIndex.value(), 0.0f);
    ctx.setPostfixText(barIndex.value(), initialStatus);
    if (onCompactProgress) { onCompactProgress(0, totalFiles); }
    if (onCompactStatusText) { onCompactStatusText(initialStatus); }
  }

  void startSpinner(std::function<void(std::string_view)> const& onCompactStatusText) {
    using namespace std::chrono_literals;
    spinnerThread = std::jthread{[this, &onCompactStatusText](std::stop_token stopToken) {
      auto const frames = std::array{'|', '/', '-', '\\'};
      auto frameIndex = std::size_t{0};
      while (
        !stopToken.stop_requested() && !spinnerStop.load(std::memory_order_acquire)
      ) {
        if (finalizingCount.load(std::memory_order_acquire) > 0) {
          auto lock = std::scoped_lock{mutex};
          auto const finalizingText = std::format("Finalizing {}", frames[frameIndex]);
          frameIndex = (frameIndex + 1) % frames.size();
          if (barIndex.has_value()) {
            ctx.setPostfixText(barIndex.value(), finalizingText);
          }
          if (onCompactStatusText) { onCompactStatusText(finalizingText); }
        }
        std::this_thread::sleep_for(120ms);
      }
    }};
  }

  void tryUpdateStatus(
    std::string_view statusText,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    if (finalizingCount.load(std::memory_order_acquire) == 0) {
      if (barIndex.has_value()) { ctx.setPostfixText(barIndex.value(), statusText); }
      if (onCompactStatusText) { onCompactStatusText(statusText); }
    }
  }

  void finish(
    std::size_t archiveCount,
    std::function<void(std::string_view)> const& onCompactStatusText
  ) {
    spinnerStop.store(true, std::memory_order_release);
    spinnerThread.request_stop();
    spinnerThread.join();
    if (barIndex.has_value()) {
      auto const completedStatus = formatCompactPackedStatus(archiveCount, archiveCount);
      ctx.setTone(barIndex.value(), progress::Tone::Success);
      ctx.setPostfixText(barIndex.value(), completedStatus);
      if (onCompactStatusText) { onCompactStatusText(completedStatus); }
    }
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
      &state.finalizingCount
    );

    if (!packRes) { return recorder.fail(index, zipPath, packRes); }

    onGroupPacked();
    recorder.succeed(index, zipPath);
    return {};
  }

private:
  void onEntryPacked() const {
    auto lock = std::scoped_lock{state.mutex};
    ++state.completedFileCount;

    auto const percent = totalFiles == 0 ? 100.0f
                                         : static_cast<float>(state.completedFileCount)
        / static_cast<float>(totalFiles)
        * 100.0f;
    auto const statusText = formatCompactPackingStatus(
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
    state.tryUpdateStatus(statusText, plan.progressCallbacks.onCompactStatusText);
  }

  void onGroupPacked() const {
    auto const completed = state.completedArchiveCount.fetch_add(1) + 1;
    auto lock = std::scoped_lock{state.mutex};
    auto const statusText = formatCompactPackingStatus(
      completed,
      archiveCount,
      state.completedFileCount,
      totalFiles
    );
    state.tryUpdateStatus(statusText, plan.progressCallbacks.onCompactStatusText);
  }
};

auto runPackTaskPlan(PackPlan const& plan, PackGroupTaskRunner const& runGroup)
  -> eh::Result<std::vector<fs::path>> {
  auto const maxParallelJobs =
    std::max<std::size_t>(1, plan.maxParallelJobs.value_or(plan.groups.size()));
  auto packResults = std::vector<eh::Result<void>>(plan.groups.size());
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto recorder = PackTaskRecorder{plan, packResults, zippedFiles};
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(plan.groups.size());

  for (auto index = std::size_t{0}; index < plan.groups.size(); ++index) {
    auto const zipName = internal::resolveZipNameForIndex(plan, index);
    auto const zipPath = plan.outputDir / zipName;
    auto const label = internal::resolveProgressLabelForIndex(plan, index);

    tasks.push_back({
      .id = std::format("archive:{}", collisionnaming::stablePathString(zipPath)),
      .label = label,
      .input = zipPath.string(),
      .run = [&, index, zipPath, label](taskexec::TaskContext& taskCtx) {
        recorder.notifyGroupStart(index);
        return runGroup(index, zipPath, label, taskCtx, recorder);
      },
    });
  }

  auto const runRes = taskexec::runTasks({
    .tasks = std::move(tasks),
    .maxConcurrency = maxParallelJobs,
    .progress = nullptr,
    .hideCursor = true,
  });

  if (runRes.canceled && runRes.attemptedCount < plan.groups.size()) {
    return eh::makeError("Packing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < packResults.size(); ++index) {
    if (runRes.attempted[index] == 0) { continue; }
    if (!packResults[index]) { return eh::makeError("{}", packResults[index].error()); }
    // A task that threw was caught by the executor with its packResults entry
    // left default-constructed (success) — never report such a run as success.
    if (!runRes.results[index]) {
      return eh::makeError("{}", runRes.results[index].error());
    }
  }

  return zippedFiles;
}

auto countPackedFiles(std::vector<std::vector<PackFileEntry>> const& groups)
  -> std::size_t {
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
  if (!runRes) { return eh::makeError("{}", runRes.error()); }

  state.finish(archiveCount, plan.progressCallbacks.onCompactStatusText);
  return runRes.value();
}

auto PackService::packGroupsFull(PackPlan const& plan)
  -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }
  fs::create_directories(plan.outputDir);

  return runPackTaskPlan(
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
    }
  );
}

namespace internal {

auto buildGroupOrdinalRangesImpl(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange> {
  auto ranges = std::vector<FileOrdinalRange>{};
  ranges.reserve(groups.size());

  auto nextOrdinal = std::size_t{1};
  for (auto const& group: groups) {
    if (group.empty()) {
      ranges.push_back({});
      continue;
    }

    auto const first = nextOrdinal;
    auto const last = first + group.size() - 1;
    ranges.push_back({first, last, group.size()});
    nextOrdinal = last + 1;
  }

  return ranges;
}

auto buildGroupOrdinalRangesImpl(std::vector<std::vector<PackFileEntry>> const& groups)
  -> std::vector<FileOrdinalRange> {
  auto ranges = std::vector<FileOrdinalRange>{};
  ranges.reserve(groups.size());

  auto nextOrdinal = std::size_t{1};
  for (auto const& group: groups) {
    if (group.empty()) {
      ranges.push_back({});
      continue;
    }

    auto const first = nextOrdinal;
    auto const last = first + group.size() - 1;
    ranges.push_back({first, last, group.size()});
    nextOrdinal = last + 1;
  }

  return ranges;
}

auto buildGroupOrdinalRanges(std::vector<std::vector<fs::path>> const& groups)
  -> std::vector<FileOrdinalRange> {
  return buildGroupOrdinalRangesImpl(groups);
}

auto buildGroupOrdinalRanges(std::vector<std::vector<PackFileEntry>> const& groups)
  -> std::vector<FileOrdinalRange> {
  return buildGroupOrdinalRangesImpl(groups);
}

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

auto resolveProgressLabelForIndex(PackPlan const& plan, std::size_t index)
  -> std::string {
  if (plan.progressLabelForIndex) { return plan.progressLabelForIndex(index); }

  return defaultProgressLabelForZipName(resolveZipNameForIndex(plan, index));
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

auto makeSubsetProgressLabelResolver(
  std::function<std::string(std::size_t)> const& originalResolver,
  std::shared_ptr<std::vector<std::size_t>> const& selectedIndexes
) -> std::function<std::string(std::size_t)> {
  if (!originalResolver) { return {}; }
  return [originalResolver, selectedIndexes](std::size_t subsetIndex) -> std::string {
    return originalResolver(selectedIndexes->at(subsetIndex));
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
    .progressLabelForIndex =
      makeSubsetProgressLabelResolver(plan.progressLabelForIndex, selectedIndexes),
    .progressCallbacks =
      {
        .onCompactProgress = plan.progressCallbacks.onCompactProgress,
        .onCompactStatusText = plan.progressCallbacks.onCompactStatusText,
      },
    .maxParallelJobs = plan.maxParallelJobs,
    .removeOnFailure = plan.removeOnFailure,
    .compact = plan.compact,
  };
}

}  // namespace internal

auto PackService::runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
  -> eh::Result<PackRunResult> {
  // Resumable execution is now handled by pack::execute().
  // This method provides simple non-resumable execution for internal use.
  auto* store = ctx.runtime.jobState.get();
  if (store == nullptr) {
    auto const packRes = packGroups(plan);
    if (!packRes) { return eh::makeError("{}", packRes.error()); }
    return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
  }

  // Fall back to non-resumable if jobState is present but caller
  // wants simple execution (only execute() handles resumable now).
  auto const packRes = packGroups(plan);
  if (!packRes) { return eh::makeError("{}", packRes.error()); }
  return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
}

auto PackService::packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  logging::ScopedTimer timer("pack.execute");
  auto const packLabel = std::format("{} group(s)", plan.groups.size());
  logging::ScopedErrorContext ctx("pack.execute", packLabel);
  if (plan.compact) { return packGroupsCompact(plan); }
  return packGroupsFull(plan);
}

auto PackService::packAllFilesInDirectory(
  fs::path const& dirPath,
  fs::path const& zipFileDir,
  std::uintmax_t maxGroupSize,
  bool recursive,
  NamingStrategy namingStrategy,
  std::optional<std::size_t> maxParallelJobs
) -> eh::Result<void> {
  auto const planRes = packer_.buildDirectoryPackPlan(
    dirPath,
    zipFileDir,
    maxGroupSize,
    recursive,
    namingStrategy,
    maxParallelJobs
  );
  if (!planRes) { return eh::makeError("{}", planRes.error()); }

  auto const packRes = packGroups(planRes.value());
  if (!packRes) { return eh::makeError("{}", packRes.error()); }

  return {};
}

auto PackService::runDirectoryPackWorkflow(
  appctx::AppContext& ctx,
  fs::path const& dirPath
) -> eh::Result<int> {
  auto const zipOutputDir = ctx.config.outputPath.value_or(dirPath / "packed");
  auto const planRes = packer_.buildDirectoryPackPlan(
    dirPath,
    zipOutputDir,
    kDefaultMaxArchiveGroupSize,
    true,
    ctx.config.forceNameConflictHandling ? NamingStrategy::FlatWithForce
                                         : NamingStrategy::Flat,
    ctx.config.maxParallelJobs,
    ctx.runtime.jobState ? std::optional<fs::path>{ctx.runtime.jobState->stateFilePath()}
                         : std::nullopt
  );
  if (!planRes) { return eh::makeError("Failed to pack files: {}", planRes.error()); }

  auto const packRes = runPackPlan(ctx, planRes.value());
  if (!packRes) { return eh::makeError("Failed to pack files: {}", packRes.error()); }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  terminal::println(
    Success,
    "All files packed successfully to: {}",
    terminal::path(zipOutputDir)
  );
  return 0;
}

}  // namespace pack
