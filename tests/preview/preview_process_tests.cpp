#include "preview/preview_process.h"

#include "core/work_dirs.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using testutils::copyFakeTool;
using testutils::ScopedEnvVar;

TEST_CASE(
  "pickPreviewWindows samples 5 uniform 10s windows on long videos",
  "[preview]"
) {
  // 2-hour video: windows at 0, (7190/4), 2*(7190/4), 3*(7190/4), 7190 seconds.
  auto const windows = preview::pickPreviewWindows(7'200'000'000);
  REQUIRE(windows.has_value());
  REQUIRE(windows->size() == 5);
  CHECK(windows->at(0).startUs == 0);
  CHECK(windows->at(0).durationUs == 10'000'000);
  CHECK(windows->at(1).startUs == 1'797'500'000);
  CHECK(windows->at(4).startUs == 7'190'000'000);
}

TEST_CASE("pickPreviewWindows compares short videos in full", "[preview]") {
  auto const windows = preview::pickPreviewWindows(40'000'000);
  REQUIRE(windows.has_value());
  REQUIRE(windows->size() == 1);
  CHECK(windows->front().startUs == 0);
  CHECK(windows->front().durationUs == 40'000'000);
}

TEST_CASE("pickPreviewWindows manual mode uses the requested range", "[preview]") {
  auto const windows =
    preview::pickPreviewWindows(7'200'000'000, std::pair{2510.0, 20.0});
  REQUIRE(windows.has_value());
  REQUIRE(windows->size() == 1);
  CHECK(windows->front().startUs == 2'510'000'000);
  CHECK(windows->front().durationUs == 20'000'000);
}

TEST_CASE("pickPreviewWindows manual mode clamps duration at the end", "[preview]") {
  auto const windows = preview::pickPreviewWindows(50'000'000, std::pair{10.0, 100.0});
  REQUIRE(windows.has_value());
  REQUIRE(windows->size() == 1);
  CHECK(windows->front().startUs == 10'000'000);
  CHECK(windows->front().durationUs == 40'000'000);
}

TEST_CASE(
  "pickPreviewWindows manual mode rejects a start beyond the duration",
  "[preview]"
) {
  auto const res = preview::pickPreviewWindows(50'000'000, std::pair{50.0, 10.0});
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().find("--start") != std::string::npos);
}

namespace {

// Fake ffmpeg/ffprobe = the shared e2e fake_media_tool binary, copied per role
// so argv[0] selects ffprobe vs ffmpeg (testutils::copyFakeTool). The ffmpeg
// side writes a fake libvmaf JSON log for scoring invocations when
// ENCRO_FAKE_FFMPEG_WRITE_VMAF=1.
void fillPreviewContext(
  appctx::AppContext& ctx,
  fs::path const& toolDir,
  std::vector<std::unique_ptr<ScopedEnvVar>>& envs,
  std::string const& codecName = "h264",
  std::string const& vmafScores = "96.0"
) {
  ctx.toolchain.ffprobePath = copyFakeTool(toolDir, "ffprobe");
  ctx.toolchain.ffmpegPath = copyFakeTool(toolDir, "ffmpeg");
  envs.emplace_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", "100.0")
  );
  envs.emplace_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_CODEC_NAME", codecName)
  );
  envs.emplace_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"));
  envs.emplace_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_VMAF_SCORES", vmafScores)
  );
}

}  // namespace

TEST_CASE("preview generates the comparison video with fake tools", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::writeTextFile(original);
  testutils::writeTextFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  {
    auto capture = testutils::StdoutCapture{temp.path / "stdout.txt"};
    auto const res = preview::run(
      ctx,
      preview::PreviewOptions{
        .original = original,
        .encoded = encoded,
        .noOpen = true,
      }
    );
    REQUIRE(res.has_value());
    CHECK(res.value() == 0);
  }
  auto const out = testutils::readTextFile(temp.path / "stdout.txt");

  // The summary prints exactly once, after the render completes — the
  // pre-render list print would make this two.
  CHECK(testutils::countOccurrences(out, "Preview windows") == 1);
  auto const listPos = out.find("Preview windows");
  auto const writtenPos = out.find("Preview written to:");
  REQUIRE(listPos != std::string::npos);
  REQUIRE(writtenPos != std::string::npos);
  CHECK(listPos < writtenPos);
  // The line carries the run's own elapsed time, in the compact form.
  auto const writtenLine = testutils::findHelpLine(out, "Preview written to:");
  REQUIRE(writtenLine.has_value());
  CHECK(testutils::hasElapsedSuffix(writtenLine.value()));

  // Default output next to the original.
  auto const outputPath = temp.path / "sample.preview.mp4";
  CHECK(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
}

TEST_CASE("preview --output overrides the default location", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::writeTextFile(original);
  testutils::writeTextFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  auto const custom = temp.path / "custom" / "comparison.mp4";
  auto const res = preview::run(
    ctx,
    preview::PreviewOptions{
      .original = original,
      .encoded = encoded,
      .output = custom,
      .noOpen = true,
    }
  );
  REQUIRE(res.has_value());
  CHECK(fs::exists(custom));
}

TEST_CASE("preview --quiet keeps the result line but drops narration", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::writeTextFile(original);
  testutils::writeTextFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  terminal::setQuiet(true);
  {
    auto capture = testutils::StdoutCapture{temp.path / "stdout.txt"};
    auto const res = preview::run(
      ctx,
      preview::PreviewOptions{
        .original = original,
        .encoded = encoded,
        .noOpen = true,
      }
    );
    REQUIRE(res.has_value());
    CHECK(res.value() == 0);
  }
  terminal::setQuiet(false);
  auto const out = testutils::readTextFile(temp.path / "stdout.txt");
  CAPTURE(out);

  // The run's final summary line bypasses the quiet gate; narration (the
  // preview window list) is suppressed.
  auto const writtenLine = testutils::findHelpLine(out, "Preview written to:");
  REQUIRE(writtenLine.has_value());
  CHECK(testutils::hasElapsedSuffix(writtenLine.value()));
  CHECK(out.find("Preview windows") == std::string::npos);
  CHECK(fs::exists(temp.path / "sample.preview.mp4"));
}

TEST_CASE("preview rejects webp inputs with a video-comparison-only error", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "anim.webp";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::writeTextFile(original);
  testutils::writeTextFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs, "webp");

  auto const res = preview::run(
    ctx,
    preview::PreviewOptions{
      .original = original,
      .encoded = encoded,
      .noOpen = true,
    }
  );
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().find("video comparison only") != std::string::npos);
}

TEST_CASE("preview fails when an input does not exist", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "missing.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::writeTextFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  auto const res = preview::run(
    ctx,
    preview::PreviewOptions{
      .original = original,
      .encoded = encoded,
      .noOpen = true,
    }
  );
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().find("does not exist") != std::string::npos);
}

TEST_CASE(
  "preview single-input mode probes and renders against window segments",
  "[preview]"
) {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  testutils::writeTextFile(original);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  SECTION("uniform windows across the full video") {
    auto const res =
      preview::run(ctx, preview::PreviewOptions{.original = original, .noOpen = true});
    REQUIRE(res.has_value());
    CHECK(res.value() == 0);

    auto const outputPath = temp.path / "sample.preview.mp4";
    CHECK(fs::exists(outputPath));
    CHECK(fs::file_size(outputPath) > 0);

    // Probe and window segments live in the scratch dir that is cleaned up.
    auto leftover = false;
    auto ec = std::error_code{};
    auto const scratch = workdirs::scratchDir();
    if (fs::is_directory(scratch, ec) && !ec) {
      for (
        auto const& entry:
        fs::directory_iterator(scratch, fs::directory_options::skip_permission_denied, ec)
      ) {
        if (entry.path().filename().string().starts_with("preview_")) { leftover = true; }
      }
    }
    CHECK_FALSE(leftover);
  }

  SECTION("manual range encodes a single window") {
    auto const res = preview::run(
      ctx,
      preview::PreviewOptions{
        .original = original,
        .startSeconds = 10.0,
        .durationSeconds = 20.0,
        .noOpen = true,
      }
    );
    REQUIRE(res.has_value());
    auto const outputPath = temp.path / "sample.preview.mp4";
    CHECK(fs::exists(outputPath));
  }
}

TEST_CASE("preview honors a bare output filename in the working directory", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  testutils::writeTextFile(original);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  // The working directory moves to the temp dir; the bare output name must
  // resolve there instead of crashing on an empty parent path.
  auto const previousCwd = fs::current_path();
  std::error_code cwdEc;
  fs::current_path(temp.path, cwdEc);
  REQUIRE_FALSE(cwdEc);
  struct CwdRestore {
    fs::path path;
    ~CwdRestore() {
      std::error_code ec;
      fs::current_path(path, ec);
    }
  } cwdRestore{previousCwd};

  auto const res = preview::run(
    ctx,
    preview::PreviewOptions{.original = original, .output = "result.mp4", .noOpen = true}
  );
  REQUIRE(res.has_value());
  CHECK(res.value() == 0);
  CHECK(fs::exists(temp.path / "result.mp4"));
}
TEST_CASE("preview single-input falls back to default CQ for short videos", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  testutils::writeTextFile(original);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);
  // Override duration below the 40s probe budget: probing is skipped and the
  // preview degrades to the default CQ with an informational note.
  envs.emplace_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", "20.0")
  );

  std::string out;
  {
    auto capture = testutils::StderrCapture{temp.path / "stderr.txt"};
    auto const res =
      preview::run(ctx, preview::PreviewOptions{.original = original, .noOpen = true});
    REQUIRE(res.has_value());
  }
  out = testutils::readTextFile(temp.path / "stderr.txt");
  CHECK(out.find("warning: Probing skipped") != std::string::npos);
  auto const outputPath = temp.path / "sample.preview.mp4";
  CHECK(fs::exists(outputPath));
}

// A stop request during a preview is the preview's own cancellation: the child
// the stop killed is not reported as a failed step and the run ends with the
// cancellation exit code. Each section stops a different step.
TEST_CASE(
  "preview reports a stop request as a cancellation, not a failure",
  "[preview][stop-signal]"
) {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  testutils::writeTextFile(original);
  // Below the 40s probe budget: probing is skipped, so the window encode is
  // the first ffmpeg call and the comparison render is the third (encode,
  // score, render).
  auto const gateFile = temp.path / "ffmpeg-gate";
  auto const countFile = temp.path / "call-count";
  auto const logPath = temp.path / "invocations.log";

  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  auto ctx = appctx::AppContext{};
  fillPreviewContext(ctx, temp.path, envs);
  envs.emplace_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", "20.0")
  );
  envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_FILE", gateFile.string())
  );
  envs.push_back(
    std::make_unique<
      ScopedEnvVar
    >("ENCRO_FAKE_FFMPEG_CALL_COUNT_FILE", countFile.string())
  );
  envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_TOOL_LOG_FILE", logPath.string())
  );
  envs.push_back(
    std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_GATE_TIMEOUT_MS", "3000")
  );
  auto const gateFromCall = [&](std::string callNumber) {
    // Blocks the given ffmpeg call (1-based, the way the fake tool counts).
    envs.push_back(
      std::make_unique<
        ScopedEnvVar
      >("ENCRO_FAKE_FFMPEG_GATE_FROM_CALL", std::move(callNumber))
    );
  };

  struct Outcome {
    std::optional<int> exitCode;
    std::string stdoutText;
    std::string stderrText;
    bool started = false;
  };
  // Runs one stopped preview and collects everything it reported: the notice
  // is a warning (stderr), the window list and result line are narration
  // (stdout), so a run that blamed the killed child shows up in one of them.
  auto runStoppedPreview = [&](std::size_t invocations, std::string_view tag) {
    auto outcome = Outcome{};
    auto const stderrPath = temp.path / std::format("stderr-{}.txt", tag);
    {
      auto requester =
        testutils::spawnGatedStop(logPath, gateFile, invocations, outcome.started);
      outcome.stdoutText = testutils::captureStdout([&] {
        auto const capture = testutils::StderrCapture{stderrPath};
        auto const res = preview::run(
          ctx,
          preview::PreviewOptions{.original = original, .noOpen = true}
        );
        outcome.exitCode =
          res.has_value() ? std::optional<int>{res.value()} : std::nullopt;
      });
    }
    outcome.stderrText = testutils::readTextFile(stderrPath);
    return outcome;
  };

  auto stopGuard = testutils::ScopedStopSignalReset{};

  SECTION("the window encode step killed by the stop") {
    gateFromCall("1");
    auto const run = runStoppedPreview(1, "window");

    REQUIRE(run.started);
    REQUIRE(run.exitCode.has_value());
    CHECK(run.exitCode.value() == stopsignal::kCanceledExitCode);
    CHECK(testutils::countOccurrences(run.stderrText, "Preview canceled by user.") == 1);
    CHECK(run.stderrText.find("Preview window encode failed") == std::string::npos);
    CHECK(run.stdoutText.find("Preview written to") == std::string::npos);
  }

  SECTION("the comparison render killed by the stop") {
    // Every invocation is slow, so the stop can land inside the render, and the
    // render is identified by its own start marker (the fake tool names it
    // after the output file) rather than by a call index: the number of window
    // and scoring calls differs between runs, and an index that silently points
    // at another call would let a stop land after the run finished.
    envs.push_back(std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_DELAY_MS", "600"));
    auto const markerDir = temp.path / "render-markers";
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFMPEG_MARKER_DIR", markerDir.string())
    );
    auto const renderOutput = temp.path / "sample.preview.mp4";

    auto started = false;
    auto exitCode = std::optional<int>{};
    auto const stderrPath = temp.path / "stderr-render.txt";
    auto captured = std::string{};
    {
      auto requester = std::jthread{[&] {
        started = testutils::waitUntil(
          [&] { return fs::exists(markerDir / renderOutput.filename()); },
          std::chrono::seconds{10},
          std::chrono::milliseconds{10}
        );
        stopsignal::requestStop();
        testutils::openGate(gateFile);
      }};
      captured = testutils::captureStdout([&] {
        auto const capture = testutils::StderrCapture{stderrPath};
        auto const res = preview::run(
          ctx,
          preview::PreviewOptions{.original = original, .noOpen = true}
        );
        exitCode = res.has_value() ? std::optional<int>{res.value()} : std::nullopt;
      });
    }

    REQUIRE(started);
    REQUIRE(exitCode.has_value());
    CHECK(exitCode.value() == stopsignal::kCanceledExitCode);
    auto const stderrText = testutils::readTextFile(stderrPath);
    CHECK(testutils::countOccurrences(stderrText, "Preview canceled by user.") == 1);
    CHECK(stderrText.find("Preview generation failed") == std::string::npos);
    CHECK(captured.find("Preview written to") == std::string::npos);
  }

  SECTION("the quality probe step killed by the stop") {
    // Long enough to probe, so the probe's ffmpeg scoring call is gated.
    envs.push_back(
      std::make_unique<ScopedEnvVar>("ENCRO_FAKE_FFPROBE_DURATION_SECS", "100.0")
    );
    gateFromCall("1");
    auto const run = runStoppedPreview(1, "probe");

    REQUIRE(run.started);
    REQUIRE(run.exitCode.has_value());
    CHECK(run.exitCode.value() == stopsignal::kCanceledExitCode);
    CHECK(testutils::countOccurrences(run.stderrText, "Preview canceled by user.") == 1);
    CHECK(run.stderrText.find("Probing skipped") == std::string::npos);
    // No plan block either: the probe was aborted, not completed and skipped.
    CHECK(run.stdoutText.find("Probed ") == std::string::npos);
    CHECK(run.stdoutText.find("Preview written to") == std::string::npos);
  }

  SECTION("a stop already pending during either input shape's first probe") {
    // No gate: with the stop pending before the run, the very first probe child
    // is aborted — the single-input run() probe and the two-input probe pair.
    auto const encodedPath = temp.path / "encoded.mp4";
    testutils::writeTextFile(encodedPath);

    for (auto const twoInputs: {false, true}) {
      // A fresh guard per iteration: a second stop request while the force-exit
      // deadline is armed exits the process (stop_signal.cpp).
      auto const stopGuard = testutils::ScopedStopSignalReset{};
      INFO("two inputs: " << twoInputs);
      stopsignal::requestStop();
      auto exitCode = std::optional<int>{};
      auto const stderrPath =
        temp.path / std::format("stderr-first-probe-{}.txt", twoInputs);
      auto const captured = testutils::captureStdout([&] {
        auto const capture = testutils::StderrCapture{stderrPath};
        auto const res = preview::run(
          ctx,
          preview::PreviewOptions{
            .original = original,
            .encoded = twoInputs ? std::optional<fs::path>{encodedPath} : std::nullopt,
            .noOpen = true,
          }
        );
        exitCode = res.has_value() ? std::optional<int>{res.value()} : std::nullopt;
      });

      REQUIRE(exitCode.has_value());
      CHECK(exitCode.value() == stopsignal::kCanceledExitCode);
      CHECK(
        testutils::countOccurrences(
          testutils::readTextFile(stderrPath),
          "Preview canceled by user."
        )
        == 1
      );
      CHECK(captured.find("Preview written to") == std::string::npos);
    }
  }

  SECTION("the two-input scoring pass ended by the stop") {
    auto const encodedPath = temp.path / "encoded.mp4";
    testutils::writeTextFile(encodedPath);
    // The scoring call is the first ffmpeg invocation of this shape.
    gateFromCall("1");

    auto started = false;
    auto exitCode = std::optional<int>{};
    auto const stderrPath = temp.path / "stderr-two-input-scoring.txt";
    auto captured = std::string{};
    {
      auto requester = testutils::spawnGatedStop(logPath, gateFile, 1, started);
      captured = testutils::captureStdout([&] {
        auto const capture = testutils::StderrCapture{stderrPath};
        auto const res = preview::run(
          ctx,
          preview::PreviewOptions{
            .original = original,
            .encoded = encodedPath,
            .noOpen = true,
          }
        );
        exitCode = res.has_value() ? std::optional<int>{res.value()} : std::nullopt;
      });
    }

    REQUIRE(started);
    REQUIRE(exitCode.has_value());
    CHECK(exitCode.value() == stopsignal::kCanceledExitCode);
    CHECK(
      testutils::countOccurrences(
        testutils::readTextFile(stderrPath),
        "Preview canceled by user."
      )
      == 1
    );
    CHECK(captured.find("Preview written to") == std::string::npos);
  }
}
