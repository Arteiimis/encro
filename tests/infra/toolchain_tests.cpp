#include "infra/toolchain.h"
#include "test_utils.h"
#include "utils/utils.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("findFFmpeg returns empty for invalid install dir", "[toolchain]") {
  TempDir temp;
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(emptyDir);

  auto const result = findFFmpeg(emptyDir);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("findFFprobe returns empty for invalid install dir", "[toolchain]") {
  TempDir temp;
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(emptyDir);

  auto const result = findFFprobe(emptyDir);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("toolchain resolve fails for empty install dir", "[toolchain]") {
  TempDir temp;
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(emptyDir);

  auto config = appctx::AppConfig{};
  config.ffmpegInstallDir = emptyDir;

  auto toolchain = appctx::ToolchainPaths{};
  auto const result = toolchain::resolve(config, toolchain);

  REQUIRE_FALSE(result);
  CHECK(result.error().find("FFmpeg not found") != std::string::npos);
}

TEST_CASE("toolchain resolve succeeds when tools are available", "[toolchain]") {
  auto config = appctx::AppConfig{};
  auto toolchainPaths = appctx::ToolchainPaths{};

  if (!findFFmpeg(std::nullopt).has_value() || !findFFprobe(std::nullopt).has_value()) {
    SUCCEED("FFmpeg/FFprobe not available on PATH; skipping.");
    return;
  }

  auto const result = toolchain::resolve(config, toolchainPaths);
  REQUIRE(result);
  CHECK(toolchainPaths.ffmpegPath.has_value());
  CHECK(toolchainPaths.ffprobePath.has_value());
}
