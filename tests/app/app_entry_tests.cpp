#include "core/path_utf8.h"
#include "app/app_entry.h"
#include "app/version.h"

#include "infra/env.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("help intro line names the release and no build time", "[appentry]") {
  auto const expected = std::string{"encro: Universal video encoder/converter/packer | "}
    + appentry::versionToken(ENCRO_VERSION, ENCRO_GIT_COMMIT);

  CHECK(appentry::helpIntroLine() == expected);
}

TEST_CASE("version token appends the commit only when there is one", "[appentry]") {
  CHECK(appentry::versionToken("2.0.3", "c0400e92") == "v2.0.3+c0400e92");
  CHECK(appentry::versionToken("2.0.3", "") == "v2.0.3");
  CHECK(appentry::versionToken("2.0.3", "none") == "v2.0.3");
}

TEST_CASE("run status maps the exit code and the stop fact", "[appentry]") {
  CHECK(appentry::runStatus(0, false) == "success");
  // A successful run stays successful even if a stop arrives in the last instant.
  CHECK(appentry::runStatus(0, true) == "success");
  CHECK(appentry::runStatus(stopsignal::kCanceledExitCode, false) == "interrupted");
  CHECK(appentry::runStatus(1, false) == "failed");
  // A genuine failure while a stop is pending still reads interrupted.
  CHECK(appentry::runStatus(1, true) == "interrupted");
}

TEST_CASE(
  "batch request resolves the packed root and clears batch-level paths",
  "[appentry][batch]"
) {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "2024-01");

  // Toolchain resolution runs as part of building the request, so the suite
  // must not depend on a real ffmpeg being installed: any program that answers
  // -version is enough.
  auto const tools = temp.path / "tools";
  fs::create_directories(tools);
  testutils::copyFakeTool(tools, "ffmpeg");
  testutils::copyFakeTool(tools, "ffprobe");
#if defined(_WIN32)
  constexpr auto kPathSeparator = ';';
#else
  constexpr auto kPathSeparator = ':';
#endif
  auto const pathGuard = testutils::ScopedEnvVar{
    "PATH",
    pathutf8::fromPath(tools)
      + kPathSeparator
      + processenv::readEnvVar("PATH").value_or("")
  };

  auto const cmd = testutils::parseArgs(
    {"encro", "batch", pathutf8::fromPath(root), "--type", "picture", "--compress"}
  );
  REQUIRE_FALSE(cmd.error.has_value());

  auto const requestRes = appentry::buildBatchRequest(cmd);
  REQUIRE(requestRes);
  auto const& request = requestRes.value();
  CHECK(request.root == root);
  CHECK(request.packedRoot == root / "packed");
  CHECK(request.baseConfig.processType == "picture");
  CHECK(request.baseConfig.compressImages);
  CHECK_FALSE(request.baseConfig.outputPath.has_value());
  CHECK_FALSE(request.baseConfig.stateFilePath.has_value());
  CHECK_FALSE(request.baseConfig.packOutputDir.has_value());
}

TEST_CASE(
  "batch request takes the output override as a plain directory",
  "[appentry][batch]"
) {
  TempDir temp;
  auto const root = temp.path / "root";
  auto const out = temp.path / "out";
  fs::create_directories(root);

  auto const cmd = testutils::parseArgs(
    {"encro",
     "batch",
     pathutf8::fromPath(root),
     "-o",
     pathutf8::fromPath(out),
     "--pack-only"}
  );
  REQUIRE_FALSE(cmd.error.has_value());

  auto const requestRes = appentry::buildBatchRequest(cmd);
  REQUIRE(requestRes);
  CHECK(requestRes.value().packedRoot == out);
}

TEST_CASE(
  "batch toolchain resolution fails up front when ffmpeg is missing",
  "[appentry][batch]"
) {
  TempDir temp;
  auto const root = temp.path / "root";
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(root);
  fs::create_directories(emptyDir);

  auto const cmd = testutils::parseArgs({
    "encro",
    "batch",
    pathutf8::fromPath(root),
    "--type",
    "picture",
    "--compress",
    "-x",
    pathutf8::fromPath(emptyDir),
  });
  REQUIRE_FALSE(cmd.error.has_value());

  auto const requestRes = appentry::buildBatchRequest(cmd);
  REQUIRE_FALSE(requestRes);
  CHECK(requestRes.error().find("FFmpeg not found") != std::string::npos);
}

TEST_CASE(
  "batch toolchain resolution is skipped for pack-only runs",
  "[appentry][batch]"
) {
  TempDir temp;
  auto const root = temp.path / "root";
  auto const emptyDir = temp.path / "empty";
  fs::create_directories(root);
  fs::create_directories(emptyDir);

  auto const cmd = testutils::parseArgs(
    {"encro",
     "batch",
     pathutf8::fromPath(root),
     "--pack-only",
     "-x",
     pathutf8::fromPath(emptyDir)}
  );
  REQUIRE_FALSE(cmd.error.has_value());

  auto const requestRes = appentry::buildBatchRequest(cmd);
  REQUIRE(requestRes);
  CHECK_FALSE(requestRes.value().toolchain.ffmpegPath.has_value());
}
