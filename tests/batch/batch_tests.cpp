#include "batch/batch.h"
#include "core/app_context.h"
#include "infra/stop_signal.h"
#include "test_utils.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using testutils::copyFakeTool;
using testutils::readTextFile;
using testutils::ScopedEnvVar;
using testutils::ScopedStopSignalReset;
using testutils::spawnGatedStop;
using testutils::writeTextFile;

namespace {

auto zipFilesIn(fs::path const& dir) -> std::vector<fs::path> {
  auto zips = std::vector<fs::path>{};
  auto ec = std::error_code{};
  if (!fs::is_directory(dir, ec) || ec) { return zips; }
  for (auto const& entry: fs::directory_iterator{dir}) {
    if (entry.path().extension() == ".zip") { zips.push_back(entry.path()); }
  }
  return zips;
}

auto makePictureCompressRequest(fs::path const& root, fs::path const& packedRoot)
  -> batch::BatchRequest {
  auto request = batch::BatchRequest{};
  request.root = root;
  request.packedRoot = packedRoot;
  request.baseConfig.processType = "picture";
  request.baseConfig.compressImages = true;
  request.baseConfig.yesToAll = true;
  return request;
}

auto namesOf(std::vector<fs::path> const& paths) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  names.reserve(paths.size());
  for (auto const& path: paths) { names.push_back(path.filename().string()); }
  return names;
}

}  // namespace

TEST_CASE("batch enumeration keeps first-level subdirectories in name order", "[batch]") {
  TempDir temp;
  fs::create_directories(temp.path / "2024-02");
  fs::create_directories(temp.path / "2024-01" / "nested");
  writeTextFile(temp.path / "loose.txt");

  auto const res = batch::enumerateSubfolders(temp.path, temp.path / "packed");
  REQUIRE(res);
  CHECK(namesOf(res.value()) == std::vector<std::string>{"2024-01", "2024-02"});
}

TEST_CASE("batch enumeration excludes hidden and packed output dirs", "[batch]") {
  TempDir temp;
  fs::create_directories(temp.path / "2024-01");
  fs::create_directories(temp.path / ".hidden");
  fs::create_directories(temp.path / ".encro");
  fs::create_directories(temp.path / "packed");

  auto const defaultRes = batch::enumerateSubfolders(temp.path, temp.path / "packed");
  REQUIRE(defaultRes);
  CHECK(namesOf(defaultRes.value()) == std::vector<std::string>{"2024-01"});

  // A custom packed root is excluded wherever it points, and only it.
  fs::create_directories(temp.path / "out");
  auto const customRes = batch::enumerateSubfolders(temp.path, temp.path / "out");
  REQUIRE(customRes);
  CHECK(namesOf(customRes.value()) == std::vector<std::string>{"2024-01", "packed"});
}

TEST_CASE("batch enumeration allows an empty root", "[batch]") {
  TempDir temp;
  writeTextFile(temp.path / "loose.txt");

  auto const res = batch::enumerateSubfolders(temp.path, temp.path / "packed");
  REQUIRE(res);
  CHECK(res.value().empty());
}

TEST_CASE("batch enumeration fails on a non-directory root", "[batch]") {
  TempDir temp;
  auto const filePath = temp.path / "root.txt";
  writeTextFile(filePath);

  auto const res = batch::enumerateSubfolders(filePath, temp.path / "packed");
  REQUIRE_FALSE(res);
}

TEST_CASE("batch pre-check matches the video encode scan", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "video";

  SECTION("a video file counts") {
    writeTextFile(temp.path / "a.mkv");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("a non-video file does not") {
    writeTextFile(temp.path / "a.txt");
    CHECK_FALSE(batch::hasProcessableMedia(config, temp.path));
  }
}

TEST_CASE("batch pre-check matches the picture compress scan", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "picture";
  config.compressImages = true;

  SECTION("a picture file counts") {
    writeTextFile(temp.path / "a.jpg");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("a video file does not") {
    writeTextFile(temp.path / "a.mkv");
    CHECK_FALSE(batch::hasProcessableMedia(config, temp.path));
  }
}

TEST_CASE("batch pre-check counts videos as tasks under video-webp", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "picture";
  config.compressImages = true;
  config.videoWebp = true;

  SECTION("a video-only subfolder counts") {
    writeTextFile(temp.path / "a.mkv");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("a picture-only subfolder counts") {
    writeTextFile(temp.path / "a.png");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("neither does not") {
    writeTextFile(temp.path / "a.txt");
    CHECK_FALSE(batch::hasProcessableMedia(config, temp.path));
  }
}

TEST_CASE("batch pre-check matches the plain picture pack scan", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "picture";

  SECTION("a picture file counts") {
    writeTextFile(temp.path / "a.jpg");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("a video file does not") {
    writeTextFile(temp.path / "a.mkv");
    CHECK_FALSE(batch::hasProcessableMedia(config, temp.path));
  }
}

TEST_CASE("batch pre-check treats any file as packable under pack-only", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.packOnly = true;

  SECTION("a loose file counts") {
    writeTextFile(temp.path / "a.bin");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("a nested file counts") {
    fs::create_directories(temp.path / "nested");
    writeTextFile(temp.path / "nested" / "b.bin");
    CHECK(batch::hasProcessableMedia(config, temp.path));
  }

  SECTION("an empty tree does not") {
    CHECK_FALSE(batch::hasProcessableMedia(config, temp.path));
  }
}

TEST_CASE("batch runs each subfolder with its own output and state", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  fs::create_directories(root / "b");
  writeTextFile(root / "a" / "one.jpg");
  writeTextFile(root / "b" / "two.jpg");

  auto request = makePictureCompressRequest(root, root / "packed");
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto const resultsRes = batch::runTasks(request);
  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 2);
  CHECK(results[0].subfolder == root / "a");
  CHECK(results[0].outcome == batch::TaskOutcome::Done);
  CHECK(results[1].outcome == batch::TaskOutcome::Done);

  CHECK(zipFilesIn(root / "packed" / "a").size() == 1);
  CHECK(zipFilesIn(root / "packed" / "b").size() == 1);
  CHECK(fs::exists(root / "a" / ".encro" / "job-state.json"));
  CHECK(fs::exists(root / "b" / ".encro" / "job-state.json"));
}

TEST_CASE("batch skips media-less subfolders with a notice", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  fs::create_directories(root / "b");
  writeTextFile(root / "a" / "one.jpg");

  auto request = makePictureCompressRequest(root, root / "packed");
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");

  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const captured =
    testutils::captureStdout([&] { resultsRes = batch::runTasks(request); });

  REQUIRE(resultsRes);
  REQUIRE(resultsRes.value().size() == 2);
  CHECK(resultsRes.value()[0].outcome == batch::TaskOutcome::Done);
  CHECK(resultsRes.value()[1].outcome == batch::TaskOutcome::Skipped);
  CHECK(captured.find("Skipping") != std::string::npos);
  CHECK(captured.find((root / "b").string()) != std::string::npos);
  CHECK_FALSE(fs::exists(root / "packed" / "b"));
}

TEST_CASE("batch keeps going after a failing subfolder and exits non-zero", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a1", "b-bad", "c1"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "one.jpg");
  }

  auto request = makePictureCompressRequest(root, root / "packed");
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");
  // Every compression whose output lives under the "b-bad" subfolder fails.
  auto const failEnv = ScopedEnvVar{"ENCRO_FAKE_FFMPEG_FAIL_MATCH", "b-bad"};

  auto const resultsRes = batch::runTasks(request);
  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 3);
  CHECK(results[0].outcome == batch::TaskOutcome::Done);
  CHECK(results[1].outcome == batch::TaskOutcome::Failed);
  CHECK(results[2].outcome == batch::TaskOutcome::Done);
  CHECK(batch::batchExitCode(results) != 0);
  CHECK(zipFilesIn(root / "packed" / "c1").size() == 1);
}

TEST_CASE("plain picture pack batch leaves no job-state files", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  writeTextFile(root / "a" / "one.jpg");

  auto request = makePictureCompressRequest(root, root / "packed");
  request.baseConfig.compressImages = false;

  auto const resultsRes = batch::runTasks(request);
  REQUIRE(resultsRes);
  REQUIRE(resultsRes.value().size() == 1);
  CHECK(resultsRes.value()[0].outcome == batch::TaskOutcome::Done);
  CHECK(zipFilesIn(root / "packed" / "a").size() == 1);
  CHECK_FALSE(fs::exists(root / "a" / ".encro"));
}

TEST_CASE("batch exit code is zero only when no subfolder failed", "[batch]") {
  auto const done = batch::SubTaskResult{.outcome = batch::TaskOutcome::Done};
  auto const skipped = batch::SubTaskResult{.outcome = batch::TaskOutcome::Skipped};
  auto const failed = batch::SubTaskResult{.outcome = batch::TaskOutcome::Failed};

  CHECK(batch::batchExitCode(std::array{done, skipped}) == 0);
  CHECK(batch::batchExitCode(std::array{done, failed}) != 0);
}

TEST_CASE("stop during a subfolder skips it and the rest", "[batch]") {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b", "c"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "one.jpg");
  }

  auto request = makePictureCompressRequest(root, root / "packed");
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");
  auto const logEnv =
    ScopedEnvVar{"ENCRO_FAKE_TOOL_LOG_FILE", (temp.path / "tool.log").string()};
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", (temp.path / "gate").string()};

  auto stoppedInFlight = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", 1, stoppedInFlight);

  auto const resultsRes = batch::runTasks(request);
  requester.join();
  REQUIRE(stoppedInFlight);

  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 3);
  CHECK(results[0].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[0].exitCode == stopsignal::kCanceledExitCode);
  CHECK(results[1].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[2].outcome == batch::TaskOutcome::Skipped);
  CHECK(batch::batchExitCode(results) == stopsignal::kCanceledExitCode);
  CHECK(zipFilesIn(root / "packed" / "b").empty());
  CHECK(zipFilesIn(root / "packed" / "c").empty());
}

TEST_CASE(
  "batch per-subfolder config redirects output and clears batch paths",
  "[batch]"
) {
  auto request = batch::BatchRequest{};
  request.root = fs::path{"root"};
  request.packedRoot = fs::path{"out"};
  request.baseConfig.processType = "picture";
  request.baseConfig.compressImages = true;
  request.baseConfig.recursive = true;
  request.baseConfig.inputPaths = {fs::path{"other.mp4"}};
  request.baseConfig.outputPath = fs::path{"legacy"};
  request.baseConfig.stateFilePath = fs::path{"state.json"};

  auto const config = batch::subConfigFor(request, fs::path{"root/2024-01"});

  CHECK(config.inputPath == fs::path{"root/2024-01"});
  CHECK(config.inputPaths.empty());
  CHECK_FALSE(config.outputPath.has_value());
  CHECK_FALSE(config.stateFilePath.has_value());
  CHECK(config.packOutputDir == fs::path{"out/2024-01"});
  // The batch's processing options carry into every subfolder config.
  CHECK(config.processType == "picture");
  CHECK(config.compressImages);
  CHECK(config.recursive);
}

TEST_CASE("batch packed root defaults to the root's packed dir", "[batch]") {
  CHECK(
    batch::resolvePackedRoot(fs::path{"root"}, std::nullopt) == fs::path{"root/packed"}
  );
  CHECK(batch::resolvePackedRoot(fs::path{"root"}, fs::path{"out"}) == fs::path{"out"});
}

TEST_CASE(
  "batch summary lists every subfolder with its status and a count line",
  "[batch]"
) {
  auto const results = std::array{
    batch::SubTaskResult{
      .subfolder = fs::path{"root/2024-01"},
      .outcome = batch::TaskOutcome::Done
    },
    batch::SubTaskResult{
      .subfolder = fs::path{"root/2024-02"},
      .outcome = batch::TaskOutcome::Failed
    },
    batch::SubTaskResult{
      .subfolder = fs::path{"root/2024-03"},
      .outcome = batch::TaskOutcome::Skipped
    },
  };

  auto const captured = testutils::captureStdout([&] { batch::printSummary(results); });
  auto const text = testutils::stripAnsi(captured);

  CHECK(text.find("2024-01") != std::string::npos);
  CHECK(text.find("done") != std::string::npos);
  CHECK(text.find("2024-02") != std::string::npos);
  CHECK(text.find("failed") != std::string::npos);
  CHECK(text.find("2024-03") != std::string::npos);
  CHECK(text.find("skipped") != std::string::npos);
  CHECK(text.find("1/3") != std::string::npos);
  // No bracketed badges on the product channel.
  CHECK(text.find('[') == std::string::npos);
}

TEST_CASE("batch with no eligible subfolders prints a notice and exits 0", "[batch]") {
  TempDir temp;
  auto request = makePictureCompressRequest(temp.path, temp.path / "packed");

  auto runRes = eh::Result<int>{};
  auto const captured =
    testutils::captureStdout([&] { runRes = batch::runBatch(request); });

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  CHECK(testutils::stripAnsi(captured).find("No subfolders") != std::string::npos);
}

TEST_CASE("batch runs the summary table and exit code over the whole run", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  writeTextFile(root / "a" / "one.jpg");

  auto request = makePictureCompressRequest(root, root / "packed");
  request.baseConfig.compressImages = false;

  auto runRes = eh::Result<int>{};
  auto const captured =
    testutils::captureStdout([&] { runRes = batch::runBatch(request); });

  REQUIRE(runRes);
  CHECK(runRes.value() == 0);
  auto const text = testutils::stripAnsi(captured);
  CHECK(text.find("done") != std::string::npos);
  CHECK(text.find("1/1") != std::string::npos);
}

TEST_CASE("a stop requested before a subfolder skips it and the rest", "[batch]") {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "one.jpg");
  }

  auto request = makePictureCompressRequest(root, root / "packed");
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");
  stopsignal::requestStop();

  auto const resultsRes = batch::runTasks(request);
  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 2);
  CHECK(results[0].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[1].outcome == batch::TaskOutcome::Skipped);
  CHECK(batch::batchExitCode(results) == stopsignal::kCanceledExitCode);
  CHECK(zipFilesIn(root / "packed" / "a").empty());
}
