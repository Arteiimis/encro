#include "pack/pack_service.h"

#include "core/archive_plan.h"
#include "core/task_executor.h"

#include "infra/stop_signal.h"
#include "pack/packer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace pack {

namespace {

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

}  // namespace

template<class Group>
auto buildGroupOrdinalRangesImpl(std::vector<Group> const& groups)
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
    .onGroupStart = {},
    .onGroupSuccess = {},
    .onGroupFailure = {},
    .onCompactProgress = plan.onCompactProgress,
    .onCompactStatusText = plan.onCompactStatusText,
    .maxParallelJobs = plan.maxParallelJobs,
    .removeOnFailure = plan.removeOnFailure,
    .compact = plan.compact,
  };
}

auto runPackPlan(appctx::AppContext& ctx, PackPlan const& plan)
  -> eh::Result<PackRunResult> {
  auto* store = ctx.runtime.jobState.get();
  if (store == nullptr) {
    auto const packRes = packGroups(plan);
    if (!packRes) { return eh::makeError("{}", packRes.error()); }
    return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
  }

  auto preparedExecution = archiveplan::prepareResumablePackExecution(*store, plan);
  if (!preparedExecution.pendingPlan.has_value()) {
    store->setStage("completed");
    return PackRunResult{};
  }

  store->setStage("packing");

  auto const packRes = packGroups(preparedExecution.pendingPlan.value());
  if (!packRes) {
    if (stopsignal::isStopRequested()) {
      store->requestCancel();
      store->markIncompleteInterrupted(preparedExecution.pendingActionIds);
      return PackRunResult{.exitCode = stopsignal::kCanceledExitCode};
    }
    return eh::makeError("{}", packRes.error());
  }

  store->setStage("completed");
  return PackRunResult{.exitCode = 0, .zippedFiles = packRes.value()};
}

auto packGroups(PackPlan const& plan) -> eh::Result<std::vector<fs::path>> {
  if (plan.groups.empty()) { return std::vector<fs::path>{}; }

  fs::create_directories(plan.outputDir);

  auto compactProgressCtx = progress::ProgressContext{};
  auto compactBarIndex = std::optional<std::size_t>{};
  auto completedFileCount = std::size_t{0};
  auto completedArchiveCount = std::atomic<std::size_t>{0};
  auto compactProgressMutex = std::mutex{};
  auto finalizingCount = std::atomic<std::size_t>{0};
  auto finalizingSpinnerStop = std::atomic<bool>{false};
  auto const compactTotalFiles = countPackedFiles(plan.groups);
  auto const archiveCount = plan.groups.size();
  if (plan.compact) {
    auto const initialStatus =
      formatCompactPackingStatus(0, archiveCount, 0, compactTotalFiles);
    compactBarIndex = compactProgressCtx.addBar(initialStatus, progress::Tone::Packing);
    compactProgressCtx.setProgress(compactBarIndex.value(), 0.0f);
    compactProgressCtx.setPostfixText(compactBarIndex.value(), initialStatus);
    if (plan.onCompactProgress) { plan.onCompactProgress(0, compactTotalFiles); }
    if (plan.onCompactStatusText) { plan.onCompactStatusText(initialStatus); }
  }

  auto finalizingSpinner = std::jthread{};
  if (plan.compact) {
    finalizingSpinner = std::jthread{[&](std::stop_token stopToken) {
      using namespace std::chrono_literals;
      auto const frames = std::array{'|', '/', '-', '\\'};
      auto frameIndex = std::size_t{0};
      while (!stopToken.stop_requested()
             && !finalizingSpinnerStop.load(std::memory_order_acquire)) {
        if (finalizingCount.load(std::memory_order_acquire) > 0) {
          auto lock = std::scoped_lock{compactProgressMutex};
          auto const statusText = formatCompactPackingStatus(
            completedArchiveCount.load(std::memory_order_acquire),
            archiveCount,
            completedFileCount,
            compactTotalFiles
          );
          auto const finalizingText =
            std::format("{} | Finalizing {}", statusText, frames[frameIndex]);
          frameIndex = (frameIndex + 1) % frames.size();
          if (compactBarIndex.has_value()) {
            compactProgressCtx.setPostfixText(compactBarIndex.value(), finalizingText);
          }
          if (plan.onCompactStatusText) { plan.onCompactStatusText(finalizingText); }
        }
        std::this_thread::sleep_for(120ms);
      }
    }};
  }

  auto const maxParallelJobs =
    std::max<std::size_t>(1, plan.maxParallelJobs.value_or(plan.groups.size()));
  auto packResults = std::vector<eh::Result<void>>(plan.groups.size());
  auto zippedFiles = std::vector<fs::path>(plan.groups.size());
  auto tasks = std::vector<taskexec::TaskSpec>{};
  tasks.reserve(plan.groups.size());

  for (auto index = std::size_t{0}; index < plan.groups.size(); ++index) {
    auto const zipName = resolveZipNameForIndex(plan, index);
    auto const zipPath = plan.outputDir / zipName;
    auto const label = resolveProgressLabelForIndex(plan, index);

    tasks.push_back(
      taskexec::TaskSpec{
        .id = std::format("pack:{}", index),
        .label = label,
        .run =
          [&, index, zipPath, label](taskexec::TaskContext& taskCtx) -> eh::Result<void> {
          if (plan.onGroupStart) { plan.onGroupStart(index); }

          auto const packRes = plan.compact
            ? packFilesToZip(
                plan.groups[index],
                zipPath,
                [&](std::size_t /*fileIndex*/, std::size_t /*fileCount*/) {
                  auto lock = std::scoped_lock{compactProgressMutex};
                  ++completedFileCount;

                  auto const percent = compactTotalFiles == 0
                    ? 100.0f
                    : static_cast<float>(completedFileCount)
                      / static_cast<float>(compactTotalFiles)
                      * 100.0f;
                  auto const statusText = formatCompactPackingStatus(
                    completedArchiveCount.load(std::memory_order_acquire),
                    archiveCount,
                    completedFileCount,
                    compactTotalFiles
                  );

                  if (compactBarIndex.has_value()) {
                    compactProgressCtx.setProgress(compactBarIndex.value(), percent);
                    compactProgressCtx.setPostfixText(
                      compactBarIndex.value(),
                      statusText
                    );
                  }

                  if (plan.onCompactProgress) {
                    plan.onCompactProgress(completedFileCount, compactTotalFiles);
                  }
                  if (plan.onCompactStatusText) { plan.onCompactStatusText(statusText); }
                },
                &finalizingCount
              )
            : packFilesToZip(plan.groups[index], zipPath, taskCtx.progress, label);

          if (!packRes) {
            if (plan.removeOnFailure) {
              auto ec = std::error_code{};
              fs::remove(zipPath, ec);
            }
            packResults[index] = packRes;
            if (plan.onGroupFailure) { plan.onGroupFailure(index, packRes.error()); }
            return eh::makeError("{}", packRes.error());
          }

          packResults[index] = {};
          zippedFiles[index] = zipPath;
          if (plan.compact) {
            auto const completed = completedArchiveCount.fetch_add(1) + 1;
            auto lock = std::scoped_lock{compactProgressMutex};
            auto const statusText = formatCompactPackingStatus(
              completed,
              archiveCount,
              completedFileCount,
              compactTotalFiles
            );
            if (compactBarIndex.has_value()) {
              compactProgressCtx.setPostfixText(compactBarIndex.value(), statusText);
            }
            if (plan.onCompactStatusText) { plan.onCompactStatusText(statusText); }
          }
          if (plan.onGroupSuccess) { plan.onGroupSuccess(index, zipPath); }
          return {};
        }
      }
    );
  }

  auto const runRes = taskexec::runTasks(
    taskexec::TaskPlan{
      .tasks = std::move(tasks),
      .maxConcurrency = maxParallelJobs,
      .progress = nullptr,
      .hideCursor = true,
    }
  );

  if (runRes.canceled && runRes.attemptedCount < plan.groups.size()) {
    return eh::makeError("Packing canceled by user.");
  }

  for (auto index = std::size_t{0}; index < packResults.size(); ++index) {
    if (runRes.attempted[index] == 0) { continue; }
    if (!packResults[index]) { return eh::makeError("{}", packResults[index].error()); }
  }

  if (plan.compact) {
    finalizingSpinnerStop.store(true, std::memory_order_release);
    finalizingSpinner.request_stop();
    finalizingSpinner.join();
  }

  if (plan.compact && compactBarIndex.has_value()) {
    auto const completedStatus = formatCompactPackedStatus(archiveCount, archiveCount);
    compactProgressCtx.setTone(compactBarIndex.value(), progress::Tone::Success);
    compactProgressCtx.setPostfixText(compactBarIndex.value(), completedStatus);
    if (plan.onCompactStatusText) { plan.onCompactStatusText(completedStatus); }
  }

  return zippedFiles;
}

}  // namespace pack
