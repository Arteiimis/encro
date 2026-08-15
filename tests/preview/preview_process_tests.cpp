#include "preview/preview_process.h"

#include "test_utils.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

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

#if defined(_WIN32)

  #include <cstdlib>
  #include <fstream>

namespace {

class ScopedEnvVar {
public:
  ScopedEnvVar(std::string name, std::string value)
    : name_(std::move(name)), hadOriginal_(false) {
    auto const original = std::getenv(name_.c_str());
    if (original != nullptr) {
      originalValue_ = original;
      hadOriginal_ = true;
    }
    _putenv_s(name_.c_str(), value.c_str());
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  auto operator=(ScopedEnvVar const&) -> ScopedEnvVar& = delete;

  ~ScopedEnvVar() {
    if (hadOriginal_) {
      _putenv_s(name_.c_str(), originalValue_.c_str());
    } else {
      _putenv_s(name_.c_str(), "");
    }
  }

private:
  std::string name_;
  std::string originalValue_;
  bool hadOriginal_;
};

auto copyFakeTool(fs::path const& dir, std::string const& name) -> fs::path {
  auto const dst = dir / (name + ".exe");
  fs::copy_file(fs::path{FAKE_TOOL_EXE_PATH}, dst, fs::copy_options::overwrite_existing);
  return dst;
}

// Fake ffmpeg/ffprobe = the e2e fake_media_tool.exe (FAKE_TOOL_EXE_PATH), copied
// per role so argv[0] selects ffprobe vs ffmpeg. The ffmpeg side writes a fake
// libvmaf JSON log for scoring invocations when ENCRO_FAKE_FFMPEG_WRITE_VMAF=1.
auto fillPreviewContext(
  appctx::AppContext& ctx,
  fs::path const& toolDir,
  std::vector<std::unique_ptr<ScopedEnvVar>>& envs,
  std::string const& codecName = "h264",
  std::string const& vmafScores = "96.0"
) -> void {
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
  testutils::touchFile(original);
  testutils::touchFile(encoded);

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
  REQUIRE(res.has_value());
  CHECK(res.value() == 0);

  // Default output next to the original.
  auto const outputPath = temp.path / "sample.preview.mp4";
  CHECK(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
}

TEST_CASE("preview --output overrides the default location", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::touchFile(original);
  testutils::touchFile(encoded);

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

TEST_CASE("preview rejects webp inputs with a video-comparison-only error", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "anim.webp";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::touchFile(original);
  testutils::touchFile(encoded);

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
  testutils::touchFile(encoded);

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

TEST_CASE("preview manual mode with --start beyond the duration fails", "[preview]") {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  testutils::touchFile(original);
  testutils::touchFile(encoded);

  auto ctx = appctx::AppContext{};
  auto envs = std::vector<std::unique_ptr<ScopedEnvVar>>{};
  fillPreviewContext(ctx, temp.path, envs);

  auto const res = preview::run(
    ctx,
    preview::PreviewOptions{
      .original = original,
      .encoded = encoded,
      .startSeconds = 9999.0,
      .durationSeconds = 10.0,
      .noOpen = true,
    }
  );
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().find("--start") != std::string::npos);
}

#endif
