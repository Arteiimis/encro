#include "video/video_process.h"

#include "core/path_roots.h"
#include "video/video_batch_execution.h"
#include "video/video_output_planning.h"
#include "video/video_workflow_utils.h"

#include "core/job_state.h"
#include "infra/terminal.h"
#include "infra/stop_signal.h"
#include "pack/pack.h"
#include "video/video_info.h"
#include "utils/utils.h"

#include <immer/vector.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;
using enum terminal::MessageKind;
using pathroots::commonAncestorPath;
using pathroots::normalizeInputRootDir;
using videoworkflow::lookupPlannedOutputFile;
using videoworkflow::maybeJobState;
using videoworkflow::withJobState;

namespace {

using ActionIdMap = videobatch::ActionIdMap;
using EncodeResultsMap = videobatch::EncodeResultsMap;
using PendingVidList = immer::vector<fs::path>;
using PendingActionIdList = immer::vector<std::string>;

template<class Ty>
auto toStdVector(immer::vector<Ty> const& values) -> std::vector<Ty> {
  auto result = std::vector<Ty>{};
  result.reserve(values.size());
  for (auto const& value: values) { result.push_back(value); }
  return result;
}

auto packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) -> int;

void printEncodingSummary(
  std::span<fs::path const> vids,
  EncodeResultsMap const& vidsRunRes
);

auto hasEncodingFailures(EncodeResultsMap const& vidsRunRes) -> bool;

}  // namespace

namespace {

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
    prepared.actionIds = prepared.actionIds.set(inputPath.value(), task.id);
    if (jobstate::needsExecution(task)) {
      prepared.pendingVids = prepared.pendingVids.push_back(inputPath.value());
      prepared.pendingActionIds = prepared.pendingActionIds.push_back(task.id);
      continue;
    }

    prepared.initialResults = prepared.initialResults.set(inputPath.value(), true);
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
  -> std::vector<fs::path> {
  terminal::println(
    Info,
    "Scanning input path for videos: {} ...",
    terminal::path(inputPath)
  );
  spdlog::info("Scanning input path: {}", inputPath.string());
  auto vids = readAllVids(ctx.config, ctx.toolchain, ctx.runtime, inputPath);
  terminal::println(
    Info,
    "Video scan completed, found {} candidate file(s).",
    terminal::count(vids.size())
  );
  spdlog::info("Scan completed: {} candidate video(s)", vids.size());
  return vids;
}

auto scanInputVideosFromFiles(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) -> std::vector<fs::path> {
  terminal::println(
    Info,
    "Scanning input files for videos: {} file(s) ...",
    terminal::count(inputPaths.size())
  );
  spdlog::info("Scanning {} provided input file(s)", inputPaths.size());
  auto vids = readAllVidsFromFiles(ctx.config, ctx.toolchain, ctx.runtime, inputPaths);
  terminal::println(
    Info,
    "Video scan completed, found {} candidate file(s).",
    terminal::count(vids.size())
  );
  spdlog::info("Scan completed from files: {} candidate video(s)", vids.size());
  return vids;
}

auto resolveMultiInputBasePath(
  appctx::AppConfig const& config,
  std::span<fs::path const> inputPaths
) -> std::optional<fs::path> {
  if (inputPaths.empty()) { return std::nullopt; }

  if (config.outputPath.has_value()) { return config.outputPath.value(); }

  auto basePath = std::optional<fs::path>{normalizeInputRootDir(inputPaths.front())};
  for (auto const& inputPath: inputPaths) {
    basePath = commonAncestorPath(basePath.value(), normalizeInputRootDir(inputPath));
    if (!basePath.has_value()) { return std::nullopt; }
  }

  return basePath.value();
}

auto maybePackOutputs(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) -> int {
  if (!ctx.config.packOutput) { return 0; }
  return packEncodedVideos(ctx, inputPath, plannedOutputFiles, vidsRunRes);
}

auto mergeEncodeResults(
  EncodeResultsMap initialResults,
  EncodeResultsMap const& runResults
) -> EncodeResultsMap {
  for (auto const& [vidPath, success]: runResults) {
    initialResults = initialResults.set(vidPath, success);
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
    auto const pendingActionIds = toStdVector(prepared.pendingActionIds);
    store.markIncompleteInterrupted(pendingActionIds);
  });

  return stopsignal::kCanceledExitCode;
}

auto maybePackWorkflowOutputs(
  appctx::AppContext& ctx,
  std::optional<fs::path> const& packInputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) -> int {
  if (!ctx.config.packOutput) { return 0; }

  if (!packInputPath.has_value()) {
    spdlog::error(
      "Multiple input files must share the same parent directory or specify "
      "--output/-o."
    );
    return 1;
  }

  return maybePackOutputs(ctx, packInputPath.value(), plannedOutputFiles, vidsRunRes);
}

auto runScannedEncodingWorkflow(
  appctx::AppContext& ctx,
  std::vector<fs::path> const& vids,
  std::optional<fs::path> const& planningRootDir,
  std::optional<fs::path> const& packInputPath,
  std::function<void()> const& onCompleted
) -> int {
  auto const plannedOutputFilesRes =
    planVideoOutputFiles(ctx.config, vids, planningRootDir);
  if (!plannedOutputFilesRes) {
    spdlog::error(plannedOutputFilesRes.error());
    return 1;
  }

  auto const& plannedOutputFiles = plannedOutputFilesRes.value();
  withJobState(ctx, [](jobstate::Store& store) { store.setStage("encoding"); });

  auto const prepared = prepareEncodeActions(ctx, vids, plannedOutputFiles);
  auto const pendingVids = toStdVector(prepared.pendingVids);
  auto const runRes = videobatch::runEncodingTasks(
    ctx,
    pendingVids,
    plannedOutputFiles,
    prepared.actionIds,
    prepared.totalActions,
    prepared.initialResults.size()
  );
  if (!runRes.has_value()) { return 0; }

  auto const vidsRunRes = mergeEncodeResults(prepared.initialResults, runRes.value());

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
      return 0;
    }
  }

  auto const packRes =
    maybePackWorkflowOutputs(ctx, packInputPath, plannedOutputFiles, vidsRunRes);
  if (packRes != 0) { return packRes; }

  withJobState(ctx, [](jobstate::Store& store) { store.setStage("completed"); });

  printEncodingSummary(vids, vidsRunRes);
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
  spdlog::debug(
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

auto packEncodedVideos(
  appctx::AppContext& ctx,
  fs::path const& inputPath,
  appctx::path_map<fs::path> const& plannedOutputFiles,
  EncodeResultsMap const& vidsRunRes
) -> int {
  spdlog::info("Packing encoded outputs for input: {}", inputPath.string());
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
  spdlog::info(
    "Packing plan: files={} output-dir={}",
    filePaths.size(),
    zipOutputDir.string()
  );

  auto const packRes = pack::execute({
    .entries = std::move(filePaths),
    .mode = pack::PackMode::Media,
    .outputDir = zipOutputDir,
    .compact = !ctx.config.fullProgress,
    .maxParallelJobs = ctx.config.maxParallelJobs,
    .jobState = ctx.runtime.jobState.get(),
  });

  if (!packRes) {
    spdlog::error("Failed to pack encoded videos: {}", packRes.error());
    return 1;
  }
  if (packRes->exitCode != 0) { return packRes->exitCode; }

  spdlog::info("Packing completed: archive-count={}", packRes->zippedFiles.size());
  return 0;
}

void printEncodingSummary(
  std::span<fs::path const> vids,
  EncodeResultsMap const& vidsRunRes
) {
  terminal::println(Success, "All encoding tasks completed.");
  terminal::println(Heading, "Summary:");
  terminal::println(Info, "  Total videos found: {}", terminal::count(vids.size()));

  auto const successCount =
    std::ranges::count_if(vidsRunRes, [](auto const& entry) { return entry.second; });
  auto const failureCount = vidsRunRes.size() - successCount;

  spdlog::info(
    "Encoding summary: total={} success={} failed={}",
    vids.size(),
    successCount,
    failureCount
  );

  terminal::println(Success, "  Successfully encoded: {}", terminal::count(successCount));
  terminal::println(Warning, "  Failed to encode: {}", terminal::count(failureCount));

  if (failureCount > 0) {
    terminal::println(Warning, "Videos that failed to encode:");
    for (auto const& [vidPath, success]: vidsRunRes) {
      if (!success) { terminal::println(Error, "  {}", terminal::path(vidPath)); }
    }
  }
}

auto hasEncodingFailures(EncodeResultsMap const& vidsRunRes) -> bool {
  return std::ranges::any_of(vidsRunRes, [](auto const& entry) { return !entry.second; });
}

}  // namespace

int handleSingleFileEncoding(appctx::AppContext& ctx, fs::path const& videoPath) {
  return handlePathEncoding(ctx, videoPath);
}

int handlePathEncoding(appctx::AppContext& ctx, fs::path const& inputPath) {
  spdlog::info("Handle path encoding: {}", inputPath.string());
  auto const vids = scanInputVideos(ctx, inputPath);

  if (vids.empty()) {
    printNoEncodableVideosMessage(ctx.config, ctx.toolchain, inputPath);
    return 0;
  }

  auto const sourceRootDir = normalizeInputRootDir(inputPath);
  return runScannedEncodingWorkflow(ctx, vids, sourceRootDir, inputPath, [&] {
    spdlog::info("Path encoding done: {}", inputPath.string());
  });
}

int handleMultiFileEncoding(
  appctx::AppContext& ctx,
  std::span<fs::path const> inputPaths
) {
  spdlog::info("Handle multi-file encoding: input-count={}", inputPaths.size());
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
    spdlog::error(
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
    spdlog::error(
      "--keep requires multiple input files to share the same parent directory."
    );
    return 1;
  }

  return runScannedEncodingWorkflow(ctx, vids, basePath, basePath, [&] {
    spdlog::info("Multi-file encoding done: input-count={}", inputPaths.size());
  });
}
