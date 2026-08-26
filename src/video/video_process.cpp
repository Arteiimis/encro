#include "video/video_process.h"

#include "core/path_roots.h"
#include "video/video_batch_execution.h"
#include "video/video_output_planning.h"
#include "video/video_workflow_utils.h"

#include "core/job_state.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "logging/log_tags.h"
#include "logging/logging.h"
#include "pack/pack.h"
#include "video/encode_probe.h"
#include "video/video_info.h"
#include "utils/utils.h"

#include <algorithm>
#include <boost/lambda2.hpp>
#include <cstdint>
#include <map>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): OOM-only fallback logger; terminate is acceptable
DEFINE_LOGGER(logtags::VIDEO_PROCESS);

namespace fs = std::filesystem;
using boost::lambda2::
  _1;  // NOLINT(bugprone-reserved-identifier): boost::lambda2's conventional placeholder name
using boost::lambda2::second;
using enum terminal::MessageKind;
using pathroots::commonAncestorPath;
using pathroots::normalizeInputRootDir;
using stopsignal::canceledExitCodeForPromptAbort;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::maybeJobState;
using videoworkflow::withJobState;

namespace {

using ActionIdMap = videobatch::ActionIdMap;
using EncodeResultsMap = videobatch::EncodeResultsMap;
using PendingVidList = std::vector<fs::path>;
using PendingActionIdList = std::vector<std::string>;
constexpr auto kVideoArchiveBaseName = std::string_view{"videos"};

int packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
);

void printEncodingSummary(
  std::span<fs::path const> vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes,
  std::span<std::string const> attentionWarnings
);

bool hasEncodingFailures(EncodeResultsMap const& vidsRunRes);

}  // namespace

namespace {

// NOLINTNEXTLINE(bugprone-exception-escape): implicit default ctor is noexcept (std containers default-construct noexcept); clang-tidy conservative check on the aggregate
struct PreparedEncodeActions {
  PendingVidList pendingVids;
  ActionIdMap actionIds;
  EncodeResultsMap initialResults;
  PendingActionIdList pendingActionIds;
  std::size_t totalActions = 0;
};

auto buildEncodeActions(
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles
) -> std::vector<jobstate::TaskRecord> {
  auto tasks = std::vector<jobstate::TaskRecord>{};
  tasks.reserve(vids.size());

  for (auto const& vidPath: vids) {
    auto const plannedOutputFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (!plannedOutputFile.has_value()) { continue; }
    tasks.push_back(jobstate::makeEncodeTask(vidPath, plannedOutputFile.value()));
  }

  return tasks;
}

auto prepareEncodeActions(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  appctx::path_map<fs::path> const& plannedOutputFiles
) -> PreparedEncodeActions {
  auto prepared = PreparedEncodeActions{};
  prepared.totalActions = vids.size();

  auto* store = maybeJobState(ctx);
  if (store == nullptr) {
    prepared.pendingVids = PendingVidList{vids.begin(), vids.end()};
    return prepared;
  }

  auto const mergedTasks =
    store->mergeTasks(buildEncodeActions(vids, plannedOutputFiles));

  for (auto const& task: mergedTasks) {
    auto const inputPath = jobstate::primarySourcePath(task);
    if (!inputPath.has_value()) { continue; }
    prepared.actionIds.emplace(inputPath.value(), task.id);
    if (jobstate::needsExecution(task)) {
      prepared.pendingVids.push_back(inputPath.value());
      prepared.pendingActionIds.push_back(task.id);
      continue;
    }

    prepared.initialResults.emplace(inputPath.value(), true);
  }

  if (!prepared.initialResults.empty()) {
    terminal::println(
      Info,
      "Recovered {} completed task(s) from saved state/output files, {} remaining.",
      terminal::count(prepared.initialResults.size()),
      terminal::count(prepared.pendingVids.size())
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
    terminal::println(
      Hint,
      "No encodable videos found in path: {}",
      terminal::path(inputPath)
    );
    return;
  }

  if (config.outputFormat == "mp4" && isHevcEncoded(toolchain, inputPath)) {
    terminal::println(
      Hint,
      "Video is already HEVC encoded: {}",
      terminal::path(inputPath)
    );
    return;
  }

  terminal::println(
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
  terminal::println(
    Info,
    "Scanning input path for videos: {} ...",
    terminal::path(inputPath)
  );
  LOG_INFO("Scanning input path: {}", inputPath.string());
  auto vids = readAllVids(ctx.config, ctx.toolchain, ctx.runtime, inputPath);
  if (!vids) { return eh::makeError("Failed to scan input videos: {}", vids.error()); }
  terminal::println(
    Info,
    "Video scan completed, found {} candidate file(s).",
    terminal::count(vids->size())
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
  terminal::println(
    Info,
    "Scanning input files for videos: {} file(s) ...",
    terminal::count(inputPaths.size())
  );
  LOG_INFO("Scanning {} provided input file(s)", inputPaths.size());
  auto vids = readAllVidsFromFiles(ctx.config, ctx.toolchain, ctx.runtime, inputPaths);
  terminal::println(
    Info,
    "Video scan completed, found {} candidate file(s).",
    terminal::count(vids.size())
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
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) {
  if (!ctx.config.packOutput) { return 0; }
  return packEncodedVideos(ctx, inputPath, plannedOutputFiles, vidsRunRes);
}

auto mergeEncodeResults(
  EncodeResultsMap initialResults,
  EncodeResultsMap const& runResults
) -> EncodeResultsMap {
  for (auto const& [vidPath, success]: runResults) {
    // insert_or_assign: run results win over recovered/initial entries,
    // preserving immer::set's last-write-wins semantics.
    initialResults.insert_or_assign(vidPath, success);
  }

  return initialResults;
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

int maybePackWorkflowOutputs(
  appctx::AppContext& ctx,
  std::optional<fs::path> const& packInputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) {
  if (!ctx.config.packOutput) { return 0; }

  if (!packInputPath.has_value()) {
    LOG_ERROR(
      "Multiple input files must share the same parent directory or specify "
      "--output/-o."
    );
    return 1;
  }

  return maybePackOutputs(ctx, packInputPath.value(), plannedOutputFiles, vidsRunRes);
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
  withJobState(ctx, [](jobstate::Store& store) { store.setStage("encoding"); });

  auto const prepared = prepareEncodeActions(ctx, vids, plannedOutputFiles);
  auto const pendingVids = prepared.pendingVids;
  auto vidsRunRes = EncodeResultsMap{};
  auto attentionWarnings = std::vector<std::string>{};
  {
    logging::ScopedTimer timer("video.encode");
    auto const encodeLabel = std::format("{} video(s)", vids.size());
    logging::ScopedErrorContext scopedCtx("video.encode", encodeLabel);
    auto const encodeJob = videobatch::EncodingBatchJob{
      .vids = pendingVids,
      .plannedOutputFiles = plannedOutputFiles,
      .actionIds = prepared.actionIds,
    };
    auto outcome = videobatch::runEncodingTasks(
      ctx,
      encodeJob,
      prepared.totalActions,
      prepared.initialResults.size()
    );
    if (!outcome.results.has_value()) { return canceledExitCodeForPromptAbort(); }
    if (outcome.dryRun) {
      LOG_INFO("Dry run completed; no files were encoded.");
      return 0;
    }
    attentionWarnings = std::move(outcome.attentionWarnings);
    vidsRunRes = mergeEncodeResults(prepared.initialResults, outcome.results.value());
  }

  if (
    auto const stopExit = maybeHandleInterruptedEncoding(ctx, prepared);
    stopExit.has_value()
  ) {
    return stopExit.value();
  }

  if (
    prepared.pendingVids.empty()
    && !prepared.initialResults.empty()
    && ctx.config.packOutput
  ) {
    auto const proceed = readUserIpt(
      ctx.config.yesToAll,
      "All encodes already complete. Do you want to proceed with packing? (Y/n): "
    );
    if (!proceed) {
      terminal::println(Warning, "Packing task canceled by user.");
      return canceledExitCodeForPromptAbort();
    }
  }

  auto const packRes =
    maybePackWorkflowOutputs(ctx, packInputPath, plannedOutputFiles, vidsRunRes);
  if (packRes != 0) { return packRes; }

  withJobState(ctx, [](jobstate::Store& store) { store.setStage("completed"); });

  printEncodingSummary(vids, plannedOutputFiles, vidsRunRes, attentionWarnings);
  if (onCompleted) { onCompleted(); }

  return hasEncodingFailures(vidsRunRes) ? 1 : 0;
}

auto collectEncodedOutputFiles(
  appctx::AppContext& ctx,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) -> std::vector<EncodedVideoPackFile> {
  constexpr auto kWebpPackMaxSize = std::uintmax_t{20ULL * 1024ULL * 1024ULL};

  auto encodedOutputFiles = std::vector<EncodedVideoPackFile>{};
  encodedOutputFiles.reserve(vidsRunRes.size());
  LOG_DEBUG(
    "Collecting encoded outputs for packing: success-map-size={}",
    vidsRunRes.size()
  );
  for (auto const& [vidPath, success]: vidsRunRes) {
    if (!success) { continue; }

    auto const outFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (!outFile.has_value() || !fs::exists(outFile.value())) { continue; }

    if (
      ctx.config.outputFormat == "webp"
      && fs::file_size(outFile.value()) >= kWebpPackMaxSize
    ) {
      terminal::println(
        Warning,
        "Skipping oversized webp for packing: {} ({} bytes)",
        terminal::path(outFile.value()),
        terminal::count(fs::file_size(outFile.value()))
      );
      continue;
    }

    encodedOutputFiles.push_back({
      .sourcePath = vidPath,
      .outputPath = outFile.value(),
    });
  }

  return encodedOutputFiles;
}

int packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) {
  logging::ScopedTimer timer("video.pack");
  auto const packPathStr = inputPath.string();
  logging::ScopedErrorContext scopedCtx("video.pack", packPathStr);
  LOG_INFO("Packing encoded outputs for input: {}", inputPath.string());
  auto const encodedOutputFiles =
    collectEncodedOutputFiles(ctx, plannedOutputFiles, vidsRunRes);
  if (encodedOutputFiles.empty()) {
    terminal::println(Hint, "No encoded output files found to pack.");
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

  if (!packRes) {
    LOG_ERROR("Failed to pack encoded videos: {}", packRes.error());
    return 1;
  }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  LOG_INFO("Packing completed: archive-count={}", packRes->zippedFiles.size());
  return 0;
}

void printEncodingSummary(
  std::span<fs::path const> vids,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes,
  std::span<std::string const> attentionWarnings
) {
  terminal::println(Success, "All encoding tasks completed.");
  terminal::println(Heading, "Summary:");

  auto const successCount = std::ranges::count_if(vidsRunRes, _1->*second);
  auto const failureCount = vidsRunRes.size() - successCount;

  LOG_INFO(
    "Encoding summary: total={} success={} failed={}",
    vids.size(),
    successCount,
    failureCount
  );

  // Align the count labels so the colons line up, matching the plan table's
  // aligned numeric columns.
  terminal::println(
    Info,
    "  {:>22}: {}",
    "Total videos found",
    terminal::count(vids.size())
  );
  terminal::println(
    Success,
    "  {:>22}: {}",
    "Successfully encoded",
    terminal::count(successCount)
  );
  terminal::println(
    Warning,
    "  {:>22}: {}",
    "Failed to encode",
    terminal::count(failureCount)
  );

  if (failureCount > 0) {
    terminal::println(Warning, "Videos that failed to encode:");
    for (auto const& [vidPath, success]: vidsRunRes) {
      if (!success) { terminal::println(Error, "  {}", terminal::path(vidPath)); }
    }
  }

  if (!attentionWarnings.empty()) {
    terminal::println(Warning, "Needs attention:");
    for (auto const& warning: attentionWarnings) {
      terminal::println(Warning, "  {}", warning);
    }
  }

  for (auto const& [vidPath, success]: vidsRunRes) {
    if (!success) { continue; }
    auto const outFile = lookupPlannedOutputFile(plannedOutputFiles, vidPath);
    if (!outFile.has_value()) { continue; }
    terminal::println(
      Hint,
      "  Compare: {}",
      encodeprobe::previewHint(vidPath, outFile.value())
    );
  }
}

bool hasEncodingFailures(EncodeResultsMap const& vidsRunRes) {
  return std::ranges::any_of(vidsRunRes, !(_1->*second));
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
    terminal::println(Error, "Error: {}", scanRes.error());
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
    terminal::println(Hint, "No encodable videos found in provided files.");
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
