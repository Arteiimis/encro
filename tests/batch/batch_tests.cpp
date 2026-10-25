#include "core/path_utf8.h"
#include "batch/batch.h"
#include "core/app_context.h"
#include "infra/stop_signal.h"
#include "infra/terminal.h"
#include "test_utils.h"

#include <array>
#include <filesystem>
#include <format>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using testutils::copyFakeProbe;
using testutils::copyFakeTool;
using testutils::countOccurrences;
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

// Picture request for the batch tests; leaving the answer unset keeps the
// workflow's confirmation enabled, which the ask-once cases drive through
// stdin (they pack without compressing, so they need no ffmpeg).
auto makePictureRequest(fs::path const& root, fs::path const& packedRoot)
  -> batch::BatchRequest {
  auto request = batch::BatchRequest{};
  request.root = root;
  request.packedRoot = packedRoot;
  request.baseConfig.processType = "picture";
  return request;
}

// The same request with compression and consent pre-set: for the cases that
// care about the work rather than about the prompt.
auto makePictureCompressRequest(fs::path const& root, fs::path const& packedRoot)
  -> batch::BatchRequest {
  auto request = makePictureRequest(root, packedRoot);
  request.baseConfig.compressImages = true;
  request.baseConfig.yesToAll = true;
  return request;
}

// Two subfolders with one picture each, both reaching the confirmation.
auto makeTwoPictureSubfolders(TempDir const& temp) -> fs::path {
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "one.jpg");
  }
  return root;
}

// Runs a batch with both product streams captured: narration, prompts and skip
// notices land on stdout, the workflows' warnings on stderr. Assertions stay
// outside the redirect windows.
template<typename Ty>
auto runWithCapturedOutputs(Ty&& action, fs::path const& scratchDir)
  -> std::pair<std::string, std::string> {
  auto const stdoutPath = scratchDir / "stdout.txt";
  auto const stderrPath = scratchDir / "stderr.txt";
  {
    auto const stdoutCapture = testutils::StdoutCapture{stdoutPath};
    auto const stderrCapture = testutils::StderrCapture{stderrPath};
    std::forward<Ty>(action)();
  }
  return {
    testutils::stripAnsi(readTextFile(stdoutPath)),
    testutils::stripAnsi(readTextFile(stderrPath))
  };
}

auto namesOf(std::vector<fs::path> const& paths) -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  names.reserve(paths.size());
  for (auto const& path: paths) { names.push_back(pathutf8::fromPath(path.filename())); }
  return names;
}

constexpr auto kBlockClosing =
  std::string_view{"The confirmation below covers every subfolder in this batch."};

// The scope block runs from its header through its closing statement; below it
// come the first subfolder's own narration and then the question. Empty when
// no block printed.
auto scopeBlockOf(std::string const& text) -> std::string {
  constexpr auto kHeader = std::string_view{"Batch: "};
  auto const start = text.find(kHeader);
  if (start == std::string::npos) { return {}; }
  auto const end = text.find(kBlockClosing, start);
  if (end == std::string::npos) { return {}; }
  return text.substr(start, end + kBlockClosing.size() - start);
}

// One batch run with stdout captured: the per-subfolder results and the
// colour-free console text the assertions read.
struct CapturedRun {
  eh::Result<std::vector<batch::SubTaskResult>> results;
  std::string text;
};

auto runCaptured(batch::BatchRequest& request) -> CapturedRun {
  auto run = CapturedRun{};
  auto const captured =
    testutils::captureStdout([&] { run.results = batch::runTasks(request); });
  run.text = testutils::stripAnsi(captured);
  return run;
}

// Two processable subfolders (2 and 1 pictures) plus one media-less subfolder,
// all reaching the packing confirmation in name order.
auto makeScopedPictureBatch(TempDir const& temp) -> fs::path {
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b", "c"}) { fs::create_directories(root / name); }
  writeTextFile(root / "a" / "one.jpg");
  writeTextFile(root / "a" / "two.jpg");
  writeTextFile(root / "b" / "one.jpg");
  return root;
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
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("a non-video file does not") {
    writeTextFile(temp.path / "a.txt");
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
  }

  SECTION("an oversized video does not count for webp output") {
    config.outputFormat = "webp";
    testutils::writeSizedFile(temp.path / "big.mkv", 33 * 1024 * 1024);
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
  }

  SECTION("a small video counts for webp output") {
    config.outputFormat = "webp";
    testutils::writeSizedFile(temp.path / "small.mkv", 128);
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }
}

TEST_CASE("batch pre-check matches the picture compress scan", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "picture";
  config.compressImages = true;

  SECTION("a picture file counts") {
    writeTextFile(temp.path / "a.jpg");
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("a video file does not") {
    writeTextFile(temp.path / "a.mkv");
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
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
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("a picture-only subfolder counts") {
    writeTextFile(temp.path / "a.png");
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("an oversized video-only subfolder does not count") {
    testutils::writeSizedFile(temp.path / "big.mkv", 33 * 1024 * 1024);
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
  }

  SECTION("neither does not") {
    writeTextFile(temp.path / "a.txt");
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
  }
}

TEST_CASE("batch pre-check treats any file as packable under pack-only", "[batch]") {
  TempDir temp;
  auto config = appctx::AppConfig{};
  config.processType = "video";
  config.packOnly = true;

  SECTION("a loose file counts") {
    writeTextFile(temp.path / "a.bin");
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("a nested file counts") {
    fs::create_directories(temp.path / "nested");
    writeTextFile(temp.path / "nested" / "b.bin");
    // Presence-only: pack-only reports 1 for "has files", never a real count.
    CHECK(batch::countProcessableMedia(config, temp.path) == 1);
  }

  SECTION("an empty tree does not") {
    CHECK(batch::countProcessableMedia(config, temp.path) == 0);
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
  CHECK(captured.find(pathutf8::fromPath((root / "b"))) != std::string::npos);
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

TEST_CASE(
  "batch exit code ignores a stop that arrives after the last subfolder",
  "[batch]"
) {
  ScopedStopSignalReset stopGuard;
  auto const done = batch::SubTaskResult{.outcome = batch::TaskOutcome::Done};
  auto const skippedByStop = batch::SubTaskResult{
    .outcome = batch::TaskOutcome::Skipped,
    .exitCode = stopsignal::kCanceledExitCode
  };

  stopsignal::requestStop();
  // Every subfolder finished before the stop: the batch is not canceled.
  CHECK(batch::batchExitCode(std::array{done}) == 0);
  // A subfolder skipped by the stop keeps the cancellation exit code.
  CHECK(batch::batchExitCode(std::array{skippedByStop}) == stopsignal::kCanceledExitCode);
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
  auto const logEnv = ScopedEnvVar{
    "ENCRO_FAKE_TOOL_LOG_FILE",
    pathutf8::fromPath((temp.path / "tool.log"))
  };
  auto const gateEnv =
    ScopedEnvVar{"ENCRO_FAKE_FFMPEG_GATE_FILE", pathutf8::fromPath((temp.path / "gate"))};

  auto stoppedInFlight = false;
  auto requester =
    spawnGatedStop(temp.path / "tool.log", temp.path / "gate", 1, stoppedInFlight);

  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const captured =
    testutils::captureStdout([&] { resultsRes = batch::runTasks(request); });
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
  // The subfolders the stop left unprocessed are skipped with a notice.
  CHECK(testutils::stripAnsi(captured).find("stop requested") != std::string::npos);
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

  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const captured =
    testutils::captureStdout([&] { resultsRes = batch::runTasks(request); });

  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 2);
  CHECK(results[0].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[0].exitCode == stopsignal::kCanceledExitCode);
  CHECK(results[1].outcome == batch::TaskOutcome::Skipped);
  CHECK(batch::batchExitCode(results) == stopsignal::kCanceledExitCode);
  CHECK(zipFilesIn(root / "packed" / "a").empty());
  CHECK(testutils::stripAnsi(captured).find("stop requested") != std::string::npos);
}

TEST_CASE("a batch asks the confirmation once and reuses the answer", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b", "c"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "one.jpg");
  }

  // One 'y' line for the whole batch: a second prompt would read EOF and
  // decline, so all three subfolders packing proves the ask happened once.
  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  auto request = makePictureRequest(root, root / "packed");
  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const captured =
    testutils::captureStdout([&] { resultsRes = batch::runTasks(request); });

  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 3);
  for (auto const& result: results) { CHECK(result.outcome == batch::TaskOutcome::Done); }
  CHECK(
    countOccurrences(
      testutils::stripAnsi(captured),
      "do you want to proceed with packing the pictures?"
    )
    == 1
  );
  CHECK(zipFilesIn(root / "packed" / "a").size() == 1);
  CHECK(zipFilesIn(root / "packed" / "b").size() == 1);
  CHECK(zipFilesIn(root / "packed" / "c").size() == 1);
}

TEST_CASE("a declined confirmation skips the batch's remaining subfolders", "[batch]") {
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto input = std::istringstream{"n\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  auto request = makePictureRequest(root, root / "packed");
  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const [stdoutText, stderrText] =
    runWithCapturedOutputs([&] { resultsRes = batch::runTasks(request); }, temp.path);

  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 2);
  CHECK(results[0].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[1].outcome == batch::TaskOutcome::Skipped);
  // A decline is not a failure and not a cancellation: single-directory parity.
  CHECK(batch::batchExitCode(results) == 0);
  CHECK(zipFilesIn(root / "packed" / "a").empty());
  CHECK(zipFilesIn(root / "packed" / "b").empty());

  // The workflow's decline notice prints once (stderr), the prompt is asked
  // once (stdout), and the subfolder the driver never asked is skipped with
  // its own notice naming it.
  CHECK(countOccurrences(stderrText, "Packing task canceled by user.") == 1);
  CHECK(countOccurrences(stdoutText, "do you want to proceed with packing") == 1);
  CHECK(stdoutText.find("confirmation declined") != std::string::npos);
  CHECK(stdoutText.find(pathutf8::fromPath((root / "b"))) != std::string::npos);
}

TEST_CASE("a stop during the confirmation keeps the stop path", "[batch]") {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto buffer = testutils::StopDuringReadBuf{"y\n"};
  auto input = std::istream{&buffer};
  auto const cinGuard = testutils::ScopedCinBuf{input};

  auto request = makePictureRequest(root, root / "packed");
  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const [stdoutText, stderrText] =
    runWithCapturedOutputs([&] { resultsRes = batch::runTasks(request); }, temp.path);

  REQUIRE(resultsRes);
  auto const& results = resultsRes.value();
  REQUIRE(results.size() == 2);
  CHECK(results[0].outcome == batch::TaskOutcome::Skipped);
  CHECK(results[0].exitCode == stopsignal::kCanceledExitCode);
  CHECK(results[1].outcome == batch::TaskOutcome::Skipped);
  CHECK(batch::batchExitCode(results) == stopsignal::kCanceledExitCode);

  // The stop is a cancellation, never a decline: the workflow picks its
  // canceled notice, the remaining subfolder is skipped as a stop, and nothing
  // is reported as declined.
  CHECK(stderrText.find("Packing canceled by user.") != std::string::npos);
  CHECK(stderrText.find("Packing task canceled by user.") == std::string::npos);
  CHECK(countOccurrences(stdoutText, "do you want to proceed with packing") == 1);
  CHECK(stdoutText.find("confirmation declined") == std::string::npos);
  CHECK(stdoutText.find("stop requested") != std::string::npos);
}

TEST_CASE("a batch with --yes asks nothing", "[batch]") {
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.yesToAll = true;

  auto resultsRes = eh::Result<std::vector<batch::SubTaskResult>>{};
  auto const captured =
    testutils::captureStdout([&] { resultsRes = batch::runTasks(request); });

  REQUIRE(resultsRes);
  for (auto const& result: resultsRes.value()) {
    CHECK(result.outcome == batch::TaskOutcome::Done);
  }
  CHECK(
    testutils::stripAnsi(captured).find("do you want to proceed with packing")
    == std::string::npos
  );
  // No confirmation means no scope block either.
  CHECK(scopeBlockOf(testutils::stripAnsi(captured)).empty());
  CHECK(zipFilesIn(root / "packed" / "a").size() == 1);
  CHECK(zipFilesIn(root / "packed" / "b").size() == 1);
}

TEST_CASE("a batch states its scope before the confirmation", "[batch]") {
  TempDir temp;
  auto const root = makeScopedPictureBatch(temp);

  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto request = makePictureRequest(root, root / "packed");
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  CHECK(run.text.find("Batch: 3 picture(s) in 2 of 3 subfolder(s)") != std::string::npos);
  CHECK(
    run.text.find(std::format("  2  {}", pathutf8::fromPath(root / "a")))
    != std::string::npos
  );
  CHECK(
    run.text.find(std::format("  1  {}", pathutf8::fromPath(root / "b")))
    != std::string::npos
  );
  CHECK(run.text.find(kBlockClosing) != std::string::npos);

  // The block states the scope before the first subfolder's own narration and
  // before the question whose scope it disambiguates.
  auto const blockAt = run.text.find("Batch: ");
  auto const firstScanAt = run.text.find("Found ");
  auto const questionAt = run.text.find("do you want to proceed with packing");
  REQUIRE(blockAt != std::string::npos);
  REQUIRE(firstScanAt != std::string::npos);
  REQUIRE(questionAt != std::string::npos);
  CHECK(blockAt < firstScanAt);
  CHECK(firstScanAt < questionAt);
}

TEST_CASE("the scope block leaves the confirmation question unchanged", "[batch]") {
  TempDir temp;
  auto const root = makeScopedPictureBatch(temp);

  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto request = makePictureRequest(root, root / "packed");
  auto const run = runCaptured(request);
  auto const block = scopeBlockOf(run.text);

  REQUIRE(run.results);
  REQUIRE_FALSE(block.empty());
  // One question, byte-identical to the single-directory wording, and the block
  // itself asks nothing and carries no badge or severity prefix.
  CHECK(
    countOccurrences(
      run.text,
      "do you want to proceed with packing the pictures? (Y/n): "
    )
    == 1
  );
  CHECK(countOccurrences(run.text, "(Y/n)") == 1);
  CHECK(block.find("warning: ") == std::string::npos);
  CHECK(block.find("error: ") == std::string::npos);
  CHECK(block.find('[') == std::string::npos);
}

TEST_CASE("a media-less subfolder has no scope row", "[batch]") {
  TempDir temp;
  auto const root = makeScopedPictureBatch(temp);

  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto request = makePictureRequest(root, root / "packed");
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  REQUIRE_FALSE(run.text.empty());
  CHECK(scopeBlockOf(run.text).find(pathutf8::fromPath(root / "c")) == std::string::npos);
  // It keeps its own skip notice at its turn.
  CHECK(
    run.text.find(
      std::format("Skipping {}: no processable media.", pathutf8::fromPath(root / "c"))
    )
    != std::string::npos
  );
}

TEST_CASE("a video run counts videos", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  fs::create_directories(root / "b");
  writeTextFile(root / "a" / "one.mkv");
  writeTextFile(root / "a" / "two.mkv");
  writeTextFile(root / "b" / "one.mkv");

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.processType = "video";
  // The question, not the encode, is what this case reaches: --crf skips the
  // size-estimation probe, and the fake probe answers the scan that still runs.
  request.baseConfig.crf = 28;
  request.toolchain.ffmpegPath = copyFakeTool(temp.path, "ffmpeg");
  request.toolchain.ffprobePath = copyFakeTool(temp.path, "ffprobe");
  auto const probeJson = copyFakeProbe(temp.path);

  auto input = std::istringstream{"n\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  CHECK(run.text.find("Batch: 3 video(s) in 2 of 2 subfolder(s)") != std::string::npos);
  CHECK(
    run.text.find(std::format("  2  {}", pathutf8::fromPath(root / "a")))
    != std::string::npos
  );
}

TEST_CASE("a video-webp picture run counts items", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  fs::create_directories(root / "vids");
  writeTextFile(root / "a" / "one.png");
  writeTextFile(root / "a" / "clip.mkv");
  writeTextFile(root / "vids" / "only.mkv");

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.videoWebp = true;

  // The packing question comes before any conversion, so a decline keeps this
  // case toolchain-free while the block still prints.
  auto input = std::istringstream{"n\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  CHECK(run.text.find("Batch: 3 item(s) in 2 of 2 subfolder(s)") != std::string::npos);
  CHECK(
    run.text.find(std::format("  1  {}", pathutf8::fromPath(root / "vids")))
    != std::string::npos
  );
}

TEST_CASE("the scope block promises scope not work", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  fs::create_directories(root / "a");
  writeTextFile(root / "a" / "one.png");

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.videoWebp = true;

  auto input = std::istringstream{"n\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto const run = runCaptured(request);
  auto const block = scopeBlockOf(run.text);

  REQUIRE(run.results);
  REQUIRE_FALSE(block.empty());
  // The numbers are scanned entries; a probe or the HEVC filter may drop some.
  // Rows carry user paths, which can hold any of these words (this suite's own
  // temp dir is named video_encoder_tests-...), so the prose lines carry the
  // rule: the header and the closing statement.
  auto const header = block.substr(0, block.find('\n'));
  CHECK(header.find("will") == std::string::npos);
  CHECK(header.find("encode") == std::string::npos);
  CHECK(header.find("convert") == std::string::npos);
  auto const closing = block.substr(block.find(kBlockClosing));
  CHECK(closing.find("will") == std::string::npos);
  CHECK(closing.find("encode") == std::string::npos);
  CHECK(closing.find("convert") == std::string::npos);
}

TEST_CASE("a pack-only batch prints no block", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b"}) {
    fs::create_directories(root / name);
    writeTextFile(root / name / "note.bin");
  }

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.processType = "video";
  request.baseConfig.packOnly = true;
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  for (auto const& result: run.results.value()) {
    CHECK(result.outcome == batch::TaskOutcome::Done);
  }
  CHECK(scopeBlockOf(run.text).empty());
  CHECK(zipFilesIn(root / "packed" / "a").size() == 1);
}

TEST_CASE("a dry-run batch prints no block", "[batch]") {
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto request = makePictureRequest(root, root / "packed");
  request.baseConfig.dryRun = true;
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  for (auto const& result: run.results.value()) {
    CHECK(result.outcome == batch::TaskOutcome::Done);
  }
  CHECK(scopeBlockOf(run.text).empty());
}

TEST_CASE("a quiet batch prints no block but still asks", "[batch]") {
  auto const terminalGuard = testutils::ScopedTerminalReset{};
  terminal::setQuiet(true);
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto input = std::istringstream{"y\n"};
  auto const cinGuard = testutils::ScopedCinBuf{input};
  auto request = makePictureRequest(root, root / "packed");
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  // The rows ride the same gate as the header: a `Plain` row would slip through
  // quiet mode and leave an orphan line above the prompt.
  CHECK(scopeBlockOf(run.text).empty());
  CHECK(
    run.text.find(std::format("  1  {}", pathutf8::fromPath(root / "a")))
    == std::string::npos
  );
  CHECK(
    run.text.find(std::format("  1  {}", pathutf8::fromPath(root / "b")))
    == std::string::npos
  );
  CHECK(
    run.text.find("do you want to proceed with packing the pictures? (Y/n): ")
    != std::string::npos
  );
}

TEST_CASE("a batch whose stop already arrived prints no block", "[batch]") {
  ScopedStopSignalReset stopGuard;
  TempDir temp;
  auto const root = makeTwoPictureSubfolders(temp);

  auto request = makePictureRequest(root, root / "packed");

  // A stop that arrived before the first subfolder leaves no confirmation to
  // read, so the block has nothing to introduce.
  stopsignal::requestStop();
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  for (auto const& result: run.results.value()) {
    CHECK(result.outcome == batch::TaskOutcome::Skipped);
  }
  CHECK(scopeBlockOf(run.text).empty());
}

TEST_CASE("a batch whose subfolders hold no media prints no block", "[batch]") {
  TempDir temp;
  auto const root = temp.path / "root";
  for (auto const* name: {"a", "b"}) { fs::create_directories(root / name); }

  auto request = makePictureRequest(root, root / "packed");
  auto const run = runCaptured(request);

  REQUIRE(run.results);
  for (auto const& result: run.results.value()) {
    CHECK(result.outcome == batch::TaskOutcome::Skipped);
  }
  CHECK(scopeBlockOf(run.text).empty());
}
