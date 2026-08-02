#include "app/pipeline.h"
#include "core/app_context.h"
#include "core/job_state.h"
#include "infra/stop_signal.h"
#include "test_utils.h"
#include "video/video_process.h"

#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

using testutils::listRegularFiles;
using testutils::ScopedStopSignalReset;
using testutils::writeTextFile;

namespace {

struct ScopedCurrentPath {
  fs::path previous;

  explicit ScopedCurrentPath(fs::path const& next): previous(fs::current_path()) {
    fs::current_path(next);
  }

  ~ScopedCurrentPath() { fs::current_path(previous); }
};

#if defined(_WIN32)
auto makeCmdScriptCommand(fs::path const& scriptPath) -> fs::path {
  return fs::path{std::format("cmd.exe /d /c call \"{}\"", scriptPath.string())};
}

void writeFakeFfmpegScript(fs::path const& scriptPath) {
  auto const script = std::format(
    R"(
@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "progressPath="
set "outputPath="
set "previousArg="

for %%A in (%*) do (
  set "currentArg=%%~A"
  if /I "!previousArg!"=="-progress" (
    set "progressPath=!currentArg!"
  )
  if /I "!currentArg!"=="-progress" (
    set "outputPath=!previousArg!"
  )
  set "previousArg=!currentArg!"
)

if defined progressPath (
  for %%I in ("%progressPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
  > "%progressPath%" (
    echo frame=10
    echo progress=end
  )
)

if not defined outputPath exit /b 2

for %%I in ("%outputPath%") do if not "%%~dpI"=="" mkdir "%%~dpI" 2>nul
> "%outputPath%" echo fake-output
exit /b 0
)"
  );
  writeTextFile(scriptPath, script);
}

void writeFakeFfprobeScript(fs::path const& scriptPath) {
  auto const script = R"(
@echo off
setlocal EnableExtensions
echo {"format":{"duration":"2.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"10","avg_frame_rate":"5/1"}]}
exit /b 0
)";
  writeTextFile(scriptPath, script);
}

void configureVideoContext(
  appctx::AppContext& ctx,
  fs::path const& ffmpegScriptPath,
  fs::path const& inputPath,
  bool packOutput = false
) {
  ctx.config.inputPath = inputPath;
  ctx.config.outputFormat = "webp";
  ctx.config.yesToAll = true;
  ctx.config.verbose = true;
  ctx.config.verboseEcho = true;
  ctx.config.packOutput = packOutput;
  ctx.toolchain.ffmpegPath = makeCmdScriptCommand(ffmpegScriptPath);
}

TEST_CASE(
  "handlePathEncoding encodes a single webp input through the orchestration path",
  "[video-process][orchestration]"
) {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const strayProgressPath = fs::current_path() / "-progress";
  auto const inputPath = temp.path / "sample.mp4";
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  writeTextFile(inputPath, "fake-video");
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, scriptPath, inputPath);

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
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.mp4", "a");
  writeTextFile(inputDir / "b.mov", "b");
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, scriptPath, inputDir, true);

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
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  auto const stateFilePath = temp.path / "encro.job-state.json";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  fs::create_directories(inputDir);
  writeTextFile(inputDir / "a.mp4", "a");
  writeTextFile(inputDir / "b.mov", "b");
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, scriptPath, inputDir);
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
  configureVideoContext(packCtx, scriptPath, inputDir, true);
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
  auto const scriptPath = temp.path / "fake_ffmpeg.cmd";
  if (fs::exists(strayProgressPath)) { fs::remove(strayProgressPath); }
  writeTextFile(inputPath, "slow-video");
  writeFakeFfmpegScript(scriptPath);

  auto ctx = appctx::AppContext{};
  configureVideoContext(ctx, scriptPath, inputPath);
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
  auto const ffprobeScriptPath = temp.path / "fake_ffprobe.cmd";
  auto const stateFilePath = temp.path / "encro.job-state.json";
  writeTextFile(inputPath, "slow-video");
  writeFakeFfprobeScript(ffprobeScriptPath);

  auto ctx = appctx::AppContext{};
  ctx.config.processType = "video";
  ctx.config.outputFormat = "webp";
  ctx.config.inputPath = inputPath;
  ctx.config.stateFilePath = stateFilePath;
  ctx.toolchain.ffprobePath = makeCmdScriptCommand(ffprobeScriptPath);

  stopsignal::requestStop();

  auto const result = pipeline::run(ctx);

  REQUIRE(result);
  CHECK(result.value() == stopsignal::kCanceledExitCode);
  CHECK_FALSE(fs::exists(temp.path / "encoded_webp" / "slow.webp"));
  CHECK_FALSE(fs::exists(stateFilePath));
}
#endif

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
