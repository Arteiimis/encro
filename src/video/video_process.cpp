#include "video/video_process.h"

#include "core/path_roots.h"
#include "video/video_batch_execution.h"
#include "video/video_output_planning.h"
#include "video/video_workflow_utils.h"

#include "core/display_text.h"
#include "core/encoding_state.h"
#include "core/job_state.h"
#include "core/media_item.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "pack/pack.h"
#include "video/encode_probe.h"
#include "video/video_info.h"
#include "utils/utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_PROCESS);

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using pathroots::commonAncestorPath;
using pathroots::normalizeInputRootDir;
using stopsignal::canceledExitCodeForPromptAbort;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::maybeJobState;
using videoworkflow::withJobState;

namespace {

using PendingActionIdList = std::vector<std::string>;
constexpr auto kVideoArchiveBaseName = std::string_view{"videos"};

int packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  std::span<appctx::EncodingStatePtr const> items
);

void printEncodingSummary(
  fs::path const& outputDir,
  std::span<appctx::EncodingStatePtr const> items,
  std::span<std::string const> attentionWarnings,
  std::size_t skippedCount,
  std::chrono::milliseconds elapsed
);

bool hasEncodingFailures(std::span<appctx::EncodingStatePtr const> items);

}  // namespace

namespace {

// NOLINTNEXTLINE(bugprone-exception-escape): implicit default ctor is noexcept (std containers default-construct noexcept); clang-tidy conservative check on the aggregate
struct PreparedEncodeActions {
  appctx::EncodingStateList pendingItems;
  PendingActionIdList pendingActionIds;
  std::size_t totalActions = 0;
  std::size_t recoveredCount = 0;
};

auto prepareEncodeActions(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items
) -> PreparedEncodeActions {
  auto prepared = PreparedEncodeActions{};
  prepared.totalActions = items.size();

  auto* store = maybeJobState(ctx);
  if (store == nullptr) {
    prepared.pendingItems.assign(items.begin(), items.end());
    return prepared;
  }

  // One planned task per item that has a planned output, in item order:
  // mergeTasks returns one merged record per planned task, in the same order,
  // so the merged tasks line up with the items that produced them.
  auto plannedItems = appctx::EncodingStateList{};
  auto plannedTasks = std::vector<jobstate::TaskRecord>{};
  plannedItems.reserve(items.size());
  plannedTasks.reserve(items.size());
  for (auto const& item: items) {
    if (!item->plannedOutputFile.has_value()) { continue; }
    plannedItems.push_back(item);
    plannedTasks.push_back(
      jobstate::makeEncodeTask(item->inputPath, item->plannedOutputFile.value())
    );
  }

  auto const mergedTasks = store->mergeTasks(plannedTasks);

  for (auto index = std::size_t{0}; index < mergedTasks.size(); ++index) {
    auto const& task = mergedTasks[index];
    auto const& item = plannedItems[index];
    item->actionId = task.id;
    if (jobstate::needsExecution(task)) {
      prepared.pendingItems.push_back(item);
      prepared.pendingActionIds.push_back(task.id);
      continue;
    }

    // Recovered from saved state: the summary counts it as encoded, and it
    // never enters a stage.
    item->outcome().state = mediaitem::ItemState::Succeeded;
    ++prepared.recoveredCount;
  }

  if (prepared.recoveredCount != 0) {
    terminal::println(
      Info,
      "Recovered {} completed task(s) from saved state/output files, {} remaining.",
      terminal::count(prepared.recoveredCount),
      terminal::count(prepared.pendingItems.size())
    );
  }

  return prepared;
}

void printNoEncodableVideosMessage(
  appctx::AppConfig const& config,
  appctx::ToolchainPaths const& toolchain,
  fs::path const& inputPath
) {
  if (!fs::is_regular_file(inputPath)) {
    terminal::messageln(
      Hint,
      "No encodable videos found in path: {}",
      terminal::path(inputPath)
    );
    return;
  }

  if (config.outputFormat == "mp4" && isHevcEncoded(toolchain, inputPath)) {
    terminal::messageln(
      Hint,
      "Video is already HEVC encoded: {}",
      terminal::path(inputPath)
    );
    return;
  }

  terminal::messageln(
    Hint,
    "No encodable videos found for file: {}",
    terminal::path(inputPath)
  );
}

auto scanInputVideos(appctx::AppContext& ctx, fs::path const& inputPath)
  -> eh::Result<std::vector<fs::path>> {
  logging::ScopedTimer timer("video.scan");
  auto const scanPathStr = inputPath.string();
  logging::ScopedErrorContext scopedCtx("video.scan", scanPathStr);
  if (terminal::streamIsTerminal(terminal::Stream::Stdout)) {
    terminal::println(Info, "Scanning for videos...");
  }
  LOG_INFO("Scanning input path: {}", inputPath.string());
  auto vids = readAllVids(ctx.config, ctx.toolchain, ctx.runtime, inputPath);
  if (!vids) { return eh::makeError("Failed to scan input videos: {}", vids.error()); }
  terminal::println(
    Info,
    "Found {} video(s) under {}.",
    terminal::count(vids->size()),
    terminal::path(inputPath)
  );
  LOG_INFO("Scan completed: {} candidate video(s)", vids->size());
  return vids.value();
}

auto scanInputVideosFromFiles(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) -> std::vector<fs::path> {
  logging::ScopedTimer timer("video.scan");
  auto const scanLabel = std::format("{} file(s)", inputPaths.size());
  logging::ScopedErrorContext scopedCtx("video.scan", scanLabel);
  if (terminal::streamIsTerminal(terminal::Stream::Stdout)) {
    terminal::println(
      Info,
      "Scanning for videos in {} file(s)...",
      terminal::count(inputPaths.size())
    );
  }
  LOG_INFO("Scanning {} provided input file(s)", inputPaths.size());
  auto vids = readAllVidsFromFiles(ctx.config, ctx.toolchain, ctx.runtime, inputPaths);
  terminal::println(
    Info,
    "Found {} video(s) from {} provided file(s).",
    terminal::count(vids.size()),
    terminal::count(inputPaths.size())
  );
  LOG_INFO("Scan completed from files: {} candidate video(s)", vids.size());
  return vids;
}

auto resolveMultiInputBasePath(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths
) -> std::optional<fs::path> {
  if (inputPaths.empty()) { return std::nullopt; }

  if (config.outputPath.has_value()) { return *config.outputPath; }

  auto basePath = std::optional<fs::path>{normalizeInputRootDir(inputPaths.front())};
  for (auto const& inputPath: inputPaths) {
    basePath = commonAncestorPath(*basePath, normalizeInputRootDir(inputPath));
    if (!basePath.has_value()) { return std::nullopt; }
  }

  return *basePath;
}

int maybePackOutputs(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  std::span<appctx::EncodingStatePtr const> items
) {
  if (!ctx.config.packOutput) { return 0; }
  return packEncodedVideos(ctx, inputPath, items);
}

auto maybeHandleInterruptedEncoding(
  appctx::AppContext& ctx,
  PreparedEncodeActions const& prepared
) -> std::optional<int> {
  if (!stopsignal::isStopRequested()) { return std::nullopt; }

  withJobState(ctx, [&](jobstate::Store& store) {
    store.requestCancel();
    auto const pendingActionIds = prepared.pendingActionIds;
    store.markIncompleteInterrupted(pendingActionIds);
  });

  return stopsignal::kCanceledExitCode;
}

// The output directory the count line names: the explicit --output, the webp
// default subdirectory, or wherever the first planned output lands.
auto summaryOutputDir(
  appctx::AppConfig const& config,
  std::optional<fs::path> const& planningRootDir,
  std::span<appctx::EncodingStatePtr const> items
) -> fs::path {
  if (auto const dir = resolveOutputRootDir(config, planningRootDir)) {
    return dir.value();
  }
  for (auto const& item: items) {
    if (item->plannedOutputFile.has_value()) {
      return item->plannedOutputFile->parent_path();
    }
  }
  return planningRootDir.value_or(fs::path{});
}

int maybePackWorkflowOutputs(
  appctx::AppContext& ctx,
  std::optional<fs::path> const& packInputPath,
  std::span<appctx::EncodingStatePtr const> items
) {
  if (!ctx.config.packOutput) { return 0; }

  if (!packInputPath.has_value()) {
    LOG_ERROR(
      "Multiple input files must share the same parent directory or specify "
      "--output/-o."
    );
    return 1;
  }

  return maybePackOutputs(ctx, packInputPath.value(), items);
}

// The items the summary and the pack list report, in the path order those two
// artefacts depend on: `fs::path::operator<`, the comparison the path-keyed
// result map they used to read from ordered by — not stablePathString, which
// case-folds. An item the run never reached stays out: it was neither
// attempted nor skipped.
auto summaryItemsBySource(appctx::EncodingStateList const& items)
  -> std::vector<appctx::EncodingStatePtr> {
  auto sortedItems = std::vector<appctx::EncodingStatePtr>{};
  sortedItems.reserve(items.size());
  for (auto const& item: items) {
    auto const state = item->outcome().state;
    if (
      state != mediaitem::ItemState::Succeeded && state != mediaitem::ItemState::Failed
    ) {
      continue;
    }
    sortedItems.push_back(item);
  }

  std::ranges::sort(sortedItems, [](auto const& lhs, auto const& rhs) {
    return lhs->source() < rhs->source();
  });
  return sortedItems;
}

int runScannedEncodingWorkflow(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  std::optional<fs::path> const& planningRootDir,
  std::optional<fs::path> const& packInputPath,
  std::function<void()> const& onCompleted
) {
  auto const plannedOutputFilesRes =
    planVideoOutputFiles(ctx.config, vids, planningRootDir);
  if (!plannedOutputFilesRes) {
    LOG_ERROR("{}", plannedOutputFilesRes.error());
    return 1;
  }

  auto const& plannedOutputFiles = plannedOutputFilesRes.value();

  // One item per unique scanned path, in input order: a repeated input is one
  // item, as the path-keyed result map it replaced counted it, and one item is
  // one pack input. Each item is alive from planning to summary: the planned
  // output, the job-state action id and the probe decision all land here, and
  // the encode stage writes its outcome back here.
  auto items = appctx::EncodingStateList{};
  auto seenPaths = std::set<fs::path>{};
  items.reserve(vids.size());
  for (auto const& vidPath: vids) {
    if (!seenPaths.insert(vidPath).second) { continue; }
    auto item = std::make_shared<appctx::EncodingState>();
    item->inputPath = vidPath;
    item->plannedOutputFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    items.push_back(std::move(item));
  }

  withJobState(ctx, [](jobstate::Store& store) { store.setStage("encoding"); });

  auto const prepared = prepareEncodeActions(ctx, items);
  auto attentionWarnings = std::vector<std::string>{};
  auto encodeElapsed = std::chrono::milliseconds{0};
  auto skippedCount = std::size_t{0};
  {
    logging::ScopedTimer timer("video.encode");
    auto const encodeLabel = std::format("{} video(s)", items.size());
    logging::ScopedErrorContext scopedCtx("video.encode", encodeLabel);
    auto outcome = videobatch::runEncodingTasks(
      ctx,
      prepared.pendingItems,
      prepared.totalActions,
      prepared.recoveredCount
    );
    if (outcome.canceled) { return canceledExitCodeForPromptAbort(); }
    if (outcome.dryRun) {
      LOG_INFO("Dry run completed; no files were encoded.");
      return 0;
    }
    attentionWarnings = std::move(outcome.attentionWarnings);
    encodeElapsed = outcome.encodeElapsed;
    skippedCount = outcome.skippedCount;
  }

  if (
    auto const stopExit = maybeHandleInterruptedEncoding(ctx, prepared);
    stopExit.has_value()
  ) {
    return stopExit.value();
  }

  if (
    prepared.pendingItems.empty() && prepared.recoveredCount != 0 && ctx.config.packOutput
  ) {
    auto const proceed = readUserIpt(
      ctx.config.yesToAll,
      "All encodes already complete. Do you want to proceed with packing? (Y/n): "
    );
    if (!proceed) {
      terminal::messageln(Warning, "Packing task canceled by user.");
      return canceledExitCodeForPromptAbort();
    }
  }

  auto const summaryItems = summaryItemsBySource(items);

  // The encode summary prints before packing, matching the order the work ran
  // in, so the packing result stays the run's last product line.
  printEncodingSummary(
    summaryOutputDir(ctx.config, planningRootDir, items),
    summaryItems,
    attentionWarnings,
    skippedCount,
    encodeElapsed
  );

  auto const packRes = maybePackWorkflowOutputs(ctx, packInputPath, summaryItems);
  if (packRes != 0) { return packRes; }

  withJobState(ctx, [](jobstate::Store& store) { store.setStage("completed"); });

  if (onCompleted) { onCompleted(); }

  return hasEncodingFailures(summaryItems) ? 1 : 0;
}

auto collectEncodedOutputFiles(
  appctx::AppContext& ctx,
  std::span<appctx::EncodingStatePtr const> items
) -> std::vector<EncodedVideoPackFile> {
  constexpr auto kWebpPackMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};

  auto encodedOutputFiles = std::vector<EncodedVideoPackFile>{};
  encodedOutputFiles.reserve(items.size());
  LOG_DEBUG("Collecting encoded outputs for packing: item-count={}", items.size());
  for (auto const& item: items) {
    if (item->outcome().state != mediaitem::ItemState::Succeeded) { continue; }

    auto const& outFile = item->plannedOutputFile;
    if (!outFile.has_value() || !fs::exists(outFile.value())) { continue; }

    if (
      ctx.config.outputFormat == "webp"
      && fs::file_size(outFile.value()) >= kWebpPackMaxSize
    ) {
      terminal::messageln(
        Warning,
        "Skipping oversized webp for packing: {} ({} bytes)",
        terminal::path(outFile.value()),
        terminal::count(fs::file_size(outFile.value()))
      );
      continue;
    }

    encodedOutputFiles.push_back({
      .outputPath = outFile.value(),
    });
  }

  return encodedOutputFiles;
}

int packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  std::span<appctx::EncodingStatePtr const> items
) {
  logging::ScopedTimer timer("video.pack");
  auto const packPathStr = inputPath.string();
  logging::ScopedErrorContext scopedCtx("video.pack", packPathStr);
  LOG_INFO("Packing encoded outputs for input: {}", inputPath.string());
  auto const encodedOutputFiles = collectEncodedOutputFiles(ctx, items);
  if (encodedOutputFiles.empty()) {
    terminal::messageln(Hint, "No encoded output files found to pack.");
    return 0;
  }

  auto const zipOutputDir = resolveVideoPackOutputPath(ctx.config, inputPath);
  fs::create_directories(zipOutputDir);

  // Flatten: collect all output file paths
  auto filePaths = std::vector<fs::path>{};
  filePaths.reserve(encodedOutputFiles.size());
  for (auto const& encodedFile: encodedOutputFiles) {
    filePaths.push_back(encodedFile.outputPath);
  }

  terminal::println(
    Info,
    "Packing {} encoded video(s)...",
    terminal::count(filePaths.size())
  );
  LOG_INFO(
    "Packing plan: files={} output-dir={}",
    filePaths.size(),
    zipOutputDir.string()
  );

  auto const packStartedAt = std::chrono::steady_clock::now();
  auto const packRes = pack::execute({
    .entries = std::move(filePaths),
    .mode = pack::PackMode::Media,
    .outputDir = zipOutputDir,
    .compact = !ctx.config.fullProgress,
    .naming =
      pack::NamingConfig{
        .namingStrategy = pack::NamingStrategy::Flat,
        .baseName = std::string{kVideoArchiveBaseName},
      },
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
  });
  auto const packElapsed = displaytext::elapsedSince(packStartedAt);

  if (!packRes) {
    LOG_ERROR("Failed to pack encoded videos: {}", packRes.error());
    return 1;
  }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  LOG_INFO("Packing completed: archive-count={}", packRes->zippedFiles.size());

  // Media mode has no line of its own otherwise: the compact bar is cleared
  // and this is the packing step's one piece of console output.
  terminal::println(
    Plain,
    "{} {} archive(s) \xE2\x86\x92 {} in {}",
    terminal::withRole(terminal::Role::Good, "Packed"),
    terminal::withRole(
      terminal::Role::Good,
      std::format("{}", packRes->zippedFiles.size())
    ),
    terminal::path(zipOutputDir),
    terminal::withRole(terminal::Role::Accent, displaytext::formatDuration(packElapsed))
  );
  return 0;
}

void printEncodingSummary(
  fs::path const& outputDir,
  std::span<appctx::EncodingStatePtr const> items,
  std::span<std::string const> attentionWarnings,
  std::size_t skippedCount,
  std::chrono::milliseconds elapsed
) {
  auto const successCount = std::ranges::count_if(items, [](auto const& item) {
    return item->outcome().state == mediaitem::ItemState::Succeeded;
  });
  auto const failureCount = items.size() - static_cast<std::size_t>(successCount);
  auto const totalCount = items.size() + skippedCount;

  LOG_INFO(
    "Encoding summary: total={} success={} failed={} skipped={}",
    totalCount,
    successCount,
    failureCount,
    skippedCount
  );

  // Outcome classes print only when they occurred, and the leading verb takes
  // the worst outcome the batch had.
  terminal::println(
    Plain,
    "{} {} \xE2\x86\x92 {} in {}",
    terminal::withRole(terminal::outcomeVerbRole(failureCount, skippedCount), "Encoded"),
    terminal::summaryCounts(
      successCount,
      totalCount,
      "videos",
      failureCount,
      skippedCount
    ),
    terminal::path(outputDir),
    terminal::withRole(terminal::Role::Accent, displaytext::formatDuration(elapsed))
  );

  if (failureCount > 0) { mediaitem::printFailures(items); }

  if (!attentionWarnings.empty()) {
    // One severity marker for the whole group: the announcement names the
    // count, and each item is indented, unprefixed, and unstyled. Repeating
    // `warning: ` on every item read as a label followed by another label.
    terminal::println(
      Warning,
      "{} item(s) need attention",
      terminal::count(attentionWarnings.size())
    );
    for (auto const& warning: attentionWarnings) {
      terminal::println(Plain, "  {}", warning);
    }
  }

  // One hint per encoded file buried the summary on large batches. The anchor
  // is the summary's own success count, so a resumed run that recovered a
  // single completed task still hints.
  if (successCount != 1) { return; }

  for (auto const& item: items) {
    if (item->outcome().state != mediaitem::ItemState::Succeeded) { continue; }
    if (!item->plannedOutputFile.has_value()) { continue; }
    terminal::println(
      Hint,
      "  Compare: {}",
      encodeprobe::previewHint(item->inputPath, item->plannedOutputFile.value())
    );
  }
}

bool hasEncodingFailures(std::span<appctx::EncodingStatePtr const> items) {
  return std::ranges::any_of(items, [](auto const& item) {
    return item->outcome().state == mediaitem::ItemState::Failed;
  });
}

}  // namespace

int handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath) {
  return handlePathEncoding(ctx, videoPath);
}

int handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) {
  LOG_INFO("Handle path encoding: {}", inputPath.string());
  auto const scanRes = scanInputVideos(ctx, inputPath);
  if (!scanRes) {
    LOG_ERROR("{}", scanRes.error());
    terminal::messageln(Error, "{}", scanRes.error());
    return 1;
  }
  auto const& vids = scanRes.value();

  if (vids.empty()) {
    printNoEncodableVideosMessage(ctx.config, ctx.toolchain, inputPath);
    return 0;
  }

  auto const sourceRootDir = normalizeInputRootDir(inputPath);
  return runScannedEncodingWorkflow(ctx, vids, sourceRootDir, inputPath, [&] {
    LOG_INFO(  // NOLINT(bugprone-lambda-function-name): SPDLOG_FUNCTION in completion lambda
      "Path encoding done: {}",
      inputPath.string()
    );
  });
}

int handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) {
  LOG_INFO("Handle multi-file encoding: input-count={}", inputPaths.size());
  auto const vids = scanInputVideosFromFiles(ctx, inputPaths);

  if (vids.empty()) {
    terminal::messageln(Hint, "No encodable videos found in provided files.");
    return 0;
  }

  auto const basePath = resolveMultiInputBasePath(ctx.config, inputPaths);
  if (
    ctx.config.outputFormat == "webp"
    && !ctx.config.outputPath.has_value()
    && !basePath.has_value()
  ) {
    LOG_ERROR(
      "Multiple input files must share the same parent directory or specify "
      "--output/-o."
    );
    return 1;
  }

  if (
    ctx.config.outputLayout == appctx::OutputLayout::Keep
    && ctx.config.outputPath.has_value()
    && !basePath.has_value()
  ) {
    LOG_ERROR("--keep requires multiple input files to share the same parent directory.");
    return 1;
  }

  return runScannedEncodingWorkflow(ctx, vids, basePath, basePath, [&] {
    LOG_INFO(  // NOLINT(bugprone-lambda-function-name): SPDLOG_FUNCTION in completion lambda
      "Multi-file encoding done: input-count={}",
      inputPaths.size()
    );
  });
}
