#include "app/pipeline.h"
#include "core/app_context.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "logging/setup.h"
#include "test_utils.h"
#include "video/video_encode_runner.h"
#include "video/video_process.h"

#include <array>
#include <filesystem>
#include <format>
#include <string>

namespace fs = std::filesystem;

using testutils::copyFakeProbe;
using testutils::copyFakeTool;
using testutils::listRegularFiles;
using testutils::readTextFile;
using testutils::ScopedEnvVar;
using testutils::ScopedStopSignalReset;
using testutils::StdoutCapture;
using testutils::writeTextFile;

namespace {

struct ScopedCurrentPath {
  fs::path previous;

  explicit ScopedCurrentPath(fs::path const& next): previous(fs::current_path()) {
    fs::current_path(next);
  }

  ~ScopedCurrentPath() { fs::current_path(previous); }
};

// Fake ffmpeg = the shared e2e fake_media_tool binary (testutils::copyFakeTool).
// The encoder side writes frame=/progress= progress entries by default;
// outputs are placeholder-sized via ENCRO_FAKE_FFMPEG_OUTPUT_BYTES.
void configureVideoContext(
  appctx::AppContext& ctx,
  fs::path const& toolDir,
  fs::path const& inputPath,
  bool packOutput = false
) {
  ctx.config.inputPath = inputPath;
  ctx.config.outputFormat = "webp";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.packOutput = packOutput;
  ctx.toolchain.ffmpegPath = copyFakeTool(toolDir, "ffmpeg");
}

TEST_CASE(
  "handlePathEncoding encodes a single webp input through the orchestration path",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  auto const inputPath = temp.path / "sample.mp4";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  writeTextFile(inputPath, "fake-video");

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, temp.path, inputPath);

  auto const result = handlePathEncoding(ctx, inputPath);
  auto const encodedFiles = listRegularFiles(temp.path / "encoded_webp");

  CHECK(result == 0);
  REQUIRE(encodedFiles.size() == 1);
  CHECK(encodedFiles.front().extension() == ".webp");
  CHECK_FALSE(fs::exists(strayProgressPath));
}

TEST_CASE(
  "handlePathEncoding packs encoded outputs when packOutput is enabled",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  auto const inputDir = temp.path / "videos";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.mp4", "a");
  writeTextFile(inputDir / "b.mov", "b");

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, temp.path, inputDir, true);

  auto const result = handlePathEncoding(ctx, inputDir);
  auto const packedFiles = listRegularFiles(inputDir / "packed");

  CHECK(result == 0);
  REQUIRE(packedFiles.size() == 1);
  CHECK(packedFiles.front().extension() == ".zip");
  CHECK(packedFiles.front().filename().string().starts_with("videos_part1["));
  CHECK_FALSE(fs::exists(strayProgressPath));
}

TEST_CASE(
  "handlePathEncoding resumes encode-only state and packs on pack-enabled run",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  auto const inputDir = temp.path / "videos";
  auto const stateFilePath = temp.path / "encro.job-state.json";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.mp4", "a");
  writeTextFile(inputDir / "b.mov", "b");

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, temp.path, inputDir);
  ctx.config.stateFilePath = stateFilePath;
  ctx.runtime.jobState = std::make_shared<jobstate::Store>(stateFilePath);
  auto const initRes = ctx.runtime.jobState->initialize(ctx.config, false);
  REQUIRE(initRes);

  auto const encodeOnlyResult = handlePathEncoding(ctx, inputDir);
  CHECK(encodeOnlyResult == 0);
  CHECK_FALSE(fs::exists(inputDir / "packed"));

  auto const encodeTaskBefore = [&]() {
    auto const tasks = ctx.runtime.jobState->tasks();
    auto const it = std::ranges::find_if(tasks, [](jobstate::TaskRecord const& task) {
      return task.kind == jobstate::kEncodeVideoKind && task.label == "a.mp4";
    });
    REQUIRE(it != tasks.end());
    return *it;
  }();
  CHECK(encodeTaskBefore.status == jobstate::TaskStatus::Succeeded);
  CHECK(encodeTaskBefore.attemptCount == 1);

  auto packCtx = appctx::AppContext{};
  configureVideoContext(packCtx, temp.path, inputDir, true);
  packCtx.config.stateFilePath = stateFilePath;
  packCtx.runtime.jobState = std::make_shared<jobstate::Store>(stateFilePath);
  auto const resumeRes = packCtx.runtime.jobState->initialize(packCtx.config, false);
  REQUIRE(resumeRes);
  CHECK(resumeRes.value());

  auto const result = handlePathEncoding(packCtx, inputDir);
  auto const packedFiles = listRegularFiles(inputDir / "packed");

  CHECK(result == 0);
  REQUIRE(packedFiles.size() == 1);
  CHECK(packedFiles.front().extension() == ".zip");

  auto const tasks = packCtx.runtime.jobState->tasks();
  auto const encodeTaskAfter =
    std::ranges::find_if(tasks, [](jobstate::TaskRecord const& task) {
      return task.kind == jobstate::kEncodeVideoKind && task.label == "a.mp4";
    });
  REQUIRE(encodeTaskAfter != tasks.end());
  CHECK(encodeTaskAfter->attemptCount == 1);
  CHECK(encodeTaskAfter->finishedAtMs == encodeTaskBefore.finishedAtMs);

  auto const archiveTaskCount =
    std::ranges::count_if(tasks, [](jobstate::TaskRecord const& task) {
      return task.kind == jobstate::kBuildArchiveKind;
    });
  CHECK(archiveTaskCount == 1);
  CHECK_FALSE(fs::exists(strayProgressPath));
}

TEST_CASE(
  "handlePathEncoding returns canceled exit code after a stop request",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  auto const inputPath = temp.path / "slow.mp4";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  writeTextFile(inputPath, "slow-video");

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, temp.path, inputPath);
  stopsignal::requestStop();

  auto const result = handlePathEncoding(ctx, inputPath);

  CHECK(result == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(temp.path / "encoded_webp" / "slow.webp"));
  CHECK_FALSE(fs::exists(strayProgressPath));
}

TEST_CASE(
  "video pipeline removes empty state file when canceled before encoding starts",
  "[video-process][orchestration][pipeline]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputPath = temp.path / "slow.mp4";
  auto const stateFilePath = temp.path / "encro.job-state.json";
  writeTextFile(inputPath, "slow-video");

  // ffprobe is never reached (stop fires before encoding starts), but the
  // toolchain must still resolve for configuration.
  auto const probeEnv = copyFakeProbe(temp.path);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "video";
  ctx.config.outputFormat = "webp";
  ctx.config.inputPath = inputPath;
  ctx.config.stateFilePath = stateFilePath;
  ctx.toolchain.ffprobePath = copyFakeTool(temp.path, "ffprobe");

  stopsignal::requestStop();

  auto const result = pipeline::run(ctx);

  REQUIRE(result);
  CHECK(result.value() == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(temp.path / "encoded_webp" / "slow.webp"));
  CHECK_FALSE(fs::exists(stateFilePath));
}

TEST_CASE(
  "verbose mode alone disables progress bars and warns",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const inputPath = temp.path / "sample.mp4";
  auto const capturePath = temp.path / "stdout.txt";
  writeTextFile(inputPath, "fake-video");

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, temp.path, inputPath);

  auto result = 0;
  {
    auto capture = StdoutCapture{capturePath};
    result = handlePathEncoding(ctx, inputPath);
  }

  CHECK(result == 0);
  auto const encodedFiles = listRegularFiles(temp.path / "encoded_webp");
  REQUIRE(encodedFiles.size() == 1);

  auto const captured = readTextFile(capturePath);
  CHECK(
    captured.find("Verbose output enabled: progress bars are disabled.")
    != std::string::npos
  );
  CHECK(captured.find("Scheduling") == std::string::npos);
}

TEST_CASE(
  "handleMultiFileEncoding rejects mixed absolute and relative inputs without a shared "
  "base",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const absoluteFile = temp.path / "absolute.mp4";
  auto const relativeFileOnDisk = temp.path / "nested" / "relative.mp4";
  writeTextFile(absoluteFile, "absolute");
  writeTextFile(relativeFileOnDisk, "relative");

  auto cwdGuard = ScopedCurrentPath{temp.path};
  auto ctx = appctx::AppContext{};
  ctx.config.outputFormat = "webp";

  auto const inputPaths =
    std::array<fs::path, 2>{absoluteFile, fs::path{"nested/relative.mp4"}};

  auto const result = handleMultiFileEncoding(ctx, inputPaths);

  CHECK(result == 1);
  CHECK_FALSE(fs::exists(temp.path / "encoded_webp"));
}

}  // namespace

TEST_CASE(
  "webp retry tier failure updates the state's subprocess cmdline",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  auto const inputPath = temp.path / "sample.mp4";
  writeTextFile(inputPath, "fake-video");

  // First call succeeds with a >20MB output (forces a retry tier), every
  // later call fails.
  auto const cntEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", (temp.path / "calls.txt").string()};
  auto const planEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_CALL_PLAN", "2-:0:1"};
  auto const bigOut = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_OUTPUT_BYTES", "22020096"};

  auto ctx = appctx::AppContext{};
  ctx.config.outputFormat = "webp";
  ctx.config.yesToAll = true;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto state = appctx::EncodingState{};
  state.inputPath = inputPath;
  state.plannedOutputFile = temp.path / "out" / "sample.webp";
  state.outputPath = temp.path / "out";

  // q=80 succeeds but overshoots the 20MB target; the retry tier (q=75) fails.
  auto const success = encodeVideo(ctx, state, {});
  CHECK_FALSE(success);

  // The state's snapshot command must reflect the failing retry tier, not the
  // stale initial q=80 command (error-visibility).
  auto lock = std::scoped_lock{state.mtx};
  CAPTURE(state.subprocessCmdline.value_or("<none>"));
  CHECK(state.subprocessCmdline.has_value());
  CHECK(state.subprocessCmdline->find("-q:v 75") != std::string::npos);
}

TEST_CASE(
  "segment end parse fallback is logged as a warning",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  auto const inputPath = temp.path / "sample.mp4";
  auto const logDir = temp.path / "logs";
  writeTextFile(inputPath, "fake-video");

  // Suppress out_time_us= so parseSegmentEndUs fails and must fall back
  // with a warning.
  auto const noEndTimeEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_PROGRESS_NO_END_TIME", "1"};
  auto const probeEnv = copyFakeProbe(temp.path);

  auto config = logging::LogConfig{
    .colorsEnabled = false,
    .customLogDir = logDir,
  };
  auto const setupRes = logging::setup(config);
  REQUIRE(setupRes.has_value());
  auto const& logPath = setupRes.value();

  auto ctx = appctx::AppContext{};
  ctx.config.outputFormat = "mp4";
  ctx.config.inputPath = inputPath;
  ctx.config.yesToAll = true;
  ctx.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");
  ctx.toolchain.ffprobePath = copyFakeTool(temp.path, "ffprobe");

  auto state = appctx::EncodingState{};
  state.inputPath = inputPath;
  state.plannedOutputFile = temp.path / "out" / "sample.mp4";
  state.outputPath = temp.path / "out";

  auto const success = encodeVideo(ctx, state, {});
  CHECK(success);

  logging::shutdown();

  auto const content = readTextFile(logPath);
  CAPTURE(content);
  CHECK(content.find("falling back to nominal duration") != std::string::npos);
}
