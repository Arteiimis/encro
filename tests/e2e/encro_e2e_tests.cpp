#include "e2e_test_utils.h"
#include "test_utils.h"

#include <boost/json.hpp>        // IWYU pragma: keep
#include <catch2/catch_all.hpp>  // IWYU pragma: keep

#include <algorithm>
#include <fstream>
#include <format>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace json = boost::json;

namespace {

bool containsStemMarker(std::string const& name, std::string_view stem) {
  return name.find(std::format("__{}__", stem)) != std::string::npos;
}

std::size_t countActualFfmpegEncodes(fs::path const& logPath) {
  auto const content = testutils::readTextFile(logPath);
  auto stream = std::istringstream{content};
  auto line = std::string{};
  auto count = std::size_t{0};

  while (std::getline(stream, line)) {
    if (!line.starts_with("ffmpeg")) { continue; }
    if (line.find("\t-version") != std::string::npos) { continue; }
    ++count;
  }

  return count;
}

auto findSingleOutputFile(fs::path const& outputDir) -> fs::path {
  auto const outputFiles = testutils::listRegularFiles(outputDir);
  REQUIRE(outputFiles.size() == 1);
  return outputFiles.front();
}

auto loadJsonObject(fs::path const& path) -> json::object {
  auto const content = testutils::readTextFile(path);
  auto const value = json::parse(content);
  REQUIRE(value.is_object());
  return value.as_object();
}

auto systemToolPath(std::string_view stem) -> fs::path {
  auto const resolved = e2e::resolveToolOnPath(stem);
  REQUIRE(resolved.has_value());
  return resolved.value();
}

bool systemToolAvailable(std::string_view stem) {
  try {
    auto const resolved = e2e::resolveToolOnPath(stem);
    if (!resolved.has_value()) { return false; }
    auto const result = e2e::runProcess(resolved.value(), {"-version"});
    return result.exitCode == 0;
  } catch (...) { return false; }
}

void requireRealToolchainOrSkip() {
  if (systemToolAvailable("ffmpeg") && systemToolAvailable("ffprobe")) { return; }
  SKIP("System FFmpeg/FFprobe not available on PATH.");
}

auto createRealSmokeVideo(
  fs::path const& outputPath,
  std::optional<std::string> const& comment = std::nullopt
) -> fs::path {
  auto args = std::vector<std::string>{
    "-y",
    "-f",
    "lavfi",
    "-i",
    "testsrc=duration=1:size=160x120:rate=5",
    "-an",
    "-c:v",
    "mpeg4",
    "-pix_fmt",
    "yuv420p",
  };

  if (comment.has_value()) {
    args.push_back("-metadata");
    args.push_back(std::format("comment={}", comment.value()));
  }

  args.push_back(outputPath.string());

  auto const result = e2e::runProcess(systemToolPath("ffmpeg"), args);
  REQUIRE_SUCCESS(result);
  REQUIRE(fs::exists(outputPath));
  return outputPath;
}

auto createRealSmokeVideoWithAudio(
  fs::path const& outputPath,
  double durationSeconds = 2.0
) -> fs::path {
  auto args = std::vector<std::string>{
    "-y",
    "-f",
    "lavfi",
    "-i",
    std::format("testsrc2=duration={}:size=160x120:rate=5", durationSeconds),
    "-f",
    "lavfi",
    "-i",
    std::format("sine=frequency=440:duration={}", durationSeconds),
    "-c:v",
    "mpeg4",
    "-c:a",
    "aac",
    "-shortest",
    "-pix_fmt",
    "yuv420p",
  };
  args.push_back(outputPath.string());

  auto const result = e2e::runProcess(systemToolPath("ffmpeg"), args);
  REQUIRE_SUCCESS(result);
  REQUIRE(fs::exists(outputPath));
  return outputPath;
}

auto probeJson(fs::path const& mediaPath, std::vector<std::string> const& args)
  -> json::object {
  auto allArgs = std::vector<std::string>{"-v", "quiet", "-print_format", "json"};
  allArgs.insert(allArgs.end(), args.begin(), args.end());
  allArgs.push_back(mediaPath.string());

  auto const result = e2e::runProcess(systemToolPath("ffprobe"), allArgs);
  REQUIRE_SUCCESS(result);
  auto const value = json::parse(result.stdoutText);
  REQUIRE(value.is_object());
  return value.as_object();
}

auto probePrimaryCodecName(fs::path const& mediaPath) -> std::string {
  auto const probed = probeJson(mediaPath, {"-show_entries", "stream=codec_name"});
  REQUIRE(probed.if_contains("streams") != nullptr);

  auto const& streams = probed.at("streams").as_array();
  REQUIRE_FALSE(streams.empty());
  REQUIRE(streams.front().is_object());

  auto const& stream = streams.front().as_object();
  REQUIRE(stream.if_contains("codec_name") != nullptr);
  auto codecName = std::string{stream.at("codec_name").as_string().c_str()};
  if (codecName == "webp_anim") { codecName = "webp"; }
  return codecName;
}

auto probeStreamTypes(fs::path const& mediaPath) -> std::vector<std::string> {
  auto const probed = probeJson(mediaPath, {"-show_entries", "stream=codec_type"});
  REQUIRE(probed.if_contains("streams") != nullptr);

  auto types = std::vector<std::string>{};
  for (auto const& stream: probed.at("streams").as_array()) {
    REQUIRE(stream.is_object());
    auto const& type = stream.as_object();
    if (type.if_contains("codec_type") != nullptr) {
      types.emplace_back(type.at("codec_type").as_string().c_str());
    }
  }
  return types;
}

auto probeFormatComment(fs::path const& mediaPath) -> std::string {
  auto const probed = probeJson(mediaPath, {"-show_entries", "format_tags=comment"});
  if (probed.if_contains("format") == nullptr || !probed.at("format").is_object()) {
    return {};
  }

  auto const& format = probed.at("format").as_object();
  if (format.if_contains("tags") == nullptr || !format.at("tags").is_object()) {
    return {};
  }

  auto const& tags = format.at("tags").as_object();
  if (tags.if_contains("comment") == nullptr) { return {}; }
  return std::string{tags.at("comment").as_string().c_str()};
}

auto listFilesWithExtension(fs::path const& dir, std::string_view extension)
  -> std::vector<fs::path> {
  auto filtered = std::vector<fs::path>{};
  for (auto const& filePath: testutils::listRegularFiles(dir)) {
    if (filePath.extension() == extension) { filtered.push_back(filePath); }
  }
  return filtered;
}

bool allFilesUseCodec(
  std::vector<fs::path> const& files,
  std::string_view expectedCodec
) {
  return std::ranges::all_of(files, [&](fs::path const& filePath) {
    return probePrimaryCodecName(filePath) == expectedCodec;
  });
}

std::size_t countLogLines(fs::path const& logPath, std::string_view needle) {
  auto const content = testutils::readTextFile(logPath);
  auto stream = std::istringstream{content};
  auto line = std::string{};
  auto count = std::size_t{0};
  while (std::getline(stream, line)) {
    if (line.find(needle) != std::string::npos) { ++count; }
  }
  return count;
}

auto findOutputMp4(fs::path const& searchRoot, fs::path const& excluded = {})
  -> std::optional<fs::path> {
  if (!fs::exists(searchRoot)) { return std::nullopt; }
  for (auto const& entry: fs::recursive_directory_iterator{searchRoot}) {
    if (
      entry.is_regular_file()
      && entry.path().extension() == ".mp4"
      && entry.path() != excluded
    ) {
      return entry.path();
    }
  }
  return std::nullopt;
}

auto segmentDirFromLog(fs::path const& logPath) -> fs::path {
  auto const content = testutils::readTextFile(logPath);
  auto stream = std::istringstream{content};
  auto line = std::string{};
  while (std::getline(stream, line)) {
    auto tokenStream = std::istringstream{line};
    auto token = std::string{};
    while (std::getline(tokenStream, token, '\t')) {
      if (token == "mpegts" && std::getline(tokenStream, token, '\t')) {
        return fs::path{token}.parent_path();
      }
    }
  }
  FAIL("No segment invocation found in fake tool log");
  return {};
}

}  // namespace

TEST_CASE(
  "encro help and version runs exit successfully with no log file hint",
  "[e2e][cli]"
) {
  REQUIRE(fs::exists(e2e::encroBinaryPath()));

  SECTION("--help") {
    auto const result = e2e::runEncro({"--help"});

    CHECK(result.exitCode == 0);
    CHECK(
      result.stdoutText.find("encro: Universal video encoder/converter/packer")
      != std::string::npos
    );
    CHECK(result.stdoutText.find("General options") != std::string::npos);
    CHECK(result.stdoutText.find("Log file:") == std::string::npos);
    CHECK(result.stderrText.find("Log file:") == std::string::npos);
  }

  SECTION("--version") {
    auto const result = e2e::runEncro({"--version"});

    REQUIRE(result.exitCode == 0);
    CHECK(result.stdoutText.find("Log file:") == std::string::npos);
    CHECK(result.stderrText.find("Log file:") == std::string::npos);
  }
}

TEST_CASE(
  "encro invalid CLI args fail with the short help hint and the stderr log hint",
  "[e2e][cli]"
) {
  auto const result = e2e::runEncro({"--nope"});

  REQUIRE(result.exitCode == 1);

  SECTION("log file hint goes to stderr, not stdout") {
    CHECK(result.stdoutText.find("Log file:") == std::string::npos);
    // encroLogTail is empty when stderr carries no "Log file: <path>" marker and
    // reports an unreadable path when the named file does not exist.
    auto const logTail = e2e::encroLogTail(result.stderrText);
    CAPTURE(result.stderrText, logTail);
    REQUIRE(logTail.starts_with("\nencro log ("));
  }

  SECTION("short help hint on stdout") {
    CHECK(result.stdoutText.find("Invalid arguments") != std::string::npos);
    CHECK(
      result.stdoutText.find("Run encro -h for help (or -hh for all options).")
      != std::string::npos
    );
    CHECK(result.stdoutText.find("General options") == std::string::npos);
  }
}

TEST_CASE("encro failed subcommand runs print the log file hint", "[e2e][cli]") {
  // Both fail inside the command body (after CLI parse): an invalid value for
  // --set, and --install without a shell. Not via failWithHint.
  auto const configRun = e2e::runEncro({"config", "--set", "jobs", "4.5"});

  REQUIRE(configRun.exitCode == 1);
  CHECK(configRun.stderrText.find("Log file:") != std::string::npos);

  auto const completionRun = e2e::runEncro({"completion", "--install"});

  REQUIRE(completionRun.exitCode == 1);
  CHECK(completionRun.stderrText.find("Log file:") != std::string::npos);
}

TEST_CASE("encro missing input prints short help hint", "[e2e][cli]") {
  auto const result = e2e::runEncro({});

  CHECK(result.exitCode == 1);
  CHECK(result.stdoutText.find("Input path is required") != std::string::npos);
  CHECK(
    result.stdoutText.find("Pass a directory or file list directly") != std::string::npos
  );
  CHECK(
    result.stdoutText.find("Run encro -h for help (or -hh for all options).")
    != std::string::npos
  );
  CHECK(result.stdoutText.find("General options") == std::string::npos);
}

TEST_CASE(
  "encro webp CLI can use the fake ffmpeg toolchain",
  "[e2e][video][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  testutils::writeTextFile(inputPath, "fake-video");

  auto toolRoot = temp.path / "fake-tools";
  SECTION("plain toolchain path") { }
  SECTION("spaced toolchain path") {
    // Quoting round-trip parity: paths with spaces must survive the
    // command-line parse/re-join chain on every platform.
    toolRoot = temp.path / "fake tools with spaces";
  }

  auto const toolchain = e2e::installFakeToolchain(toolRoot);
  REQUIRE(fs::exists(toolchain.ffmpegPath));
  REQUIRE(fs::exists(toolchain.ffprobePath));

  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  auto const outputDir = temp.path / "encoded_webp";
  CAPTURE(result.stdoutText, result.stderrText);
  REQUIRE(result.exitCode == 0);
  CHECK(result.stderrText.find("Log file:") == std::string::npos);
  REQUIRE(fs::exists(outputDir));

  auto outputFiles = std::vector<fs::path>{};
  for (auto const& entry: fs::directory_iterator{outputDir}) {
    if (entry.is_regular_file()) { outputFiles.push_back(entry.path()); }
  }

  REQUIRE(outputFiles.size() == 1);
  CHECK(outputFiles.front().extension() == ".webp");
  CHECK(containsStemMarker(outputFiles.front().filename().string(), "sample"));
  CHECK(fs::file_size(outputFiles.front()) > 0);
}

TEST_CASE(
  "encro real ffmpeg smoke converts generated mp4 with metadata comment to webp",
  "[e2e][smoke][real-ffmpeg][video]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const inputPath = temp.path / "commented.mp4";
  auto const comment = std::string{"lowres bad anatomy text error low quality"};
  createRealSmokeVideo(inputPath, comment);

  CHECK(probeFormatComment(inputPath).find("error") != std::string::npos);

  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
  });

  REQUIRE_SUCCESS(result);
  auto const outputPath = findSingleOutputFile(temp.path / "encoded_webp");
  CHECK(outputPath.extension() == ".webp");
  CHECK(fs::file_size(outputPath) > 0);
  CHECK(probePrimaryCodecName(outputPath) == "webp");
}

TEST_CASE(
  "encro real ffmpeg smoke converts mp4 with audio to h264 mp4 keeping audio",
  "[e2e][smoke][real-ffmpeg][mp4]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const inputPath = temp.path / "withaudio.mp4";
  createRealSmokeVideoWithAudio(inputPath);

  // NVENC needs a GPU CI runners lack; libx264 encodes h264 on any host
  // (libx265 hits EPERM filtering on GitHub runners).
  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--video-codec",
    "libx264",
  });

  CAPTURE(result.stdoutText, result.stderrText);
  REQUIRE(result.exitCode == 0);
  auto const outputPath = findOutputMp4(temp.path, inputPath);
  REQUIRE(outputPath.has_value());
  CHECK(outputPath->extension() == ".mp4");
  CHECK(fs::file_size(outputPath.value()) > 0);
  CHECK(probePrimaryCodecName(outputPath.value()) == "h264");
  auto const streamTypes = probeStreamTypes(outputPath.value());
  CHECK(std::ranges::find(streamTypes, "audio") != streamTypes.end());
}

TEST_CASE(
  "encro parallel jobs overlap slow encodes",
  "[e2e][video][parallel][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "inputs";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir);
  testutils::writeTextFile(inputDir / "alpha.avi", "fake-video");
  testutils::writeTextFile(inputDir / "beta.avi", "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const concurrencyDir = temp.path / "concurrency";
  fs::create_directories(concurrencyDir);
  auto const env = std::map<std::string, std::string>{
    {{
       "ENCRO_FAKE_FFMPEG_DELAY_MS",
       "3000",
     },
     {
       "ENCRO_FAKE_FFMPEG_CONCURRENCY_DIR",
       concurrencyDir.string(),
     }}
  };
  auto const args = std::vector<std::string>{
    "-y",
    "-i",
    inputDir.string(),
    "-f",
    "webp",
    "-j",
    "2",
    "-o",
    outputDir.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto const result = e2e::runEncro(args, std::nullopt, env);

  REQUIRE_SUCCESS(result);
  auto const outputFiles = listFilesWithExtension(outputDir, ".webp");
  REQUIRE(outputFiles.size() == 2);
  CHECK(fs::file_size(outputFiles[0]) > 0);
  CHECK(fs::file_size(outputFiles[1]) > 0);
  // The two 3 s encodes must have overlapped: the fake tool records each
  // delayed invocation's [start, end] window in its own file under
  // concurrencyDir, and parallel scheduling shows up as a long overlap
  // between two processes. Wall-clock thresholds would couple the assertion
  // to machine load (parallel shards, busy CI), so the overlap itself is the
  // load-independent proof.
  auto intervals = std::map<long, std::pair<std::int64_t, std::int64_t>>{};
  for (auto const& entry: fs::directory_iterator{concurrencyDir}) {
    if (!entry.is_regular_file()) { continue; }
    auto const pid = std::stol(entry.path().filename().string());
    auto lines = std::vector<std::int64_t>{};
    auto stream = std::ifstream{entry.path()};
    for (auto ts = std::int64_t{0}; stream >> ts;) { lines.push_back(ts); }
    if (lines.size() >= 2) { intervals[pid] = {lines.front(), lines.back()}; }
  }
  auto maxOverlapMs = std::int64_t{0};
  auto pids = std::vector<long>{};
  for (auto const& [pidA, a]: intervals) {
    pids.push_back(pidA);
    for (auto const& [pidB, b]: intervals) {
      if (pidA >= pidB) { continue; }
      auto const overlap = std::min(a.second, b.second) - std::max(a.first, b.first);
      maxOverlapMs = std::max(maxOverlapMs, overlap);
    }
  }
  // Two fully-overlapped 3 s encodes share ~3 s; a serialized execution
  // leaves zero overlap. Allow scheduling slack.
  CHECK(pids.size() >= 2);
  CHECK(maxOverlapMs >= 2500);
}

TEST_CASE(
  "encro ffmpeg failures return non-zero, keep successes and store task state",
  "[e2e][video][failure][multi-input][fake-toolchain]"
) {
  TempDir temp;
  auto const statePath = temp.path / "encro.job-state.json";
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");

  SECTION("single input") {
    auto const inputPath = temp.path / "sample.avi";
    testutils::writeTextFile(inputPath, "fake-video");

    auto const result = e2e::runEncro(
      {
        "-y",
        "-i",
        inputPath.string(),
        "-f",
        "webp",
        "-j",
        "1",
        "--state-file",
        statePath.string(),
        "--ffmpeg-path",
        toolchain.root.string(),
      },
      std::nullopt,
      {
        {"ENCRO_FAKE_FFMPEG_EXIT_CODE", "17"},
        {"ENCRO_FAKE_FFMPEG_STDERR", "Option fake not found."},
      }
    );

    REQUIRE(result.exitCode == 1);
    CHECK(result.stdoutText.find("Failed to encode: 1") != std::string::npos);
    CHECK(result.stderrText.find("Log file:") != std::string::npos);
    REQUIRE(fs::exists(statePath));

    auto const state = loadJsonObject(statePath);
    REQUIRE(state.if_contains("tasks") != nullptr);
    auto const& tasks = state.at("tasks").as_array();
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().as_object().at("status").as_string() == "failed");
    CHECK(testutils::listRegularFiles(temp.path / "encoded_webp").empty());
  }

  SECTION("multiple inputs") {
    auto const inputA = temp.path / "inputs" / "alpha.avi";
    auto const inputB = temp.path / "inputs" / "beta.avi";
    testutils::writeTextFile(inputA, "fake-video");
    testutils::writeTextFile(inputB, "fake-video");

    auto const result = e2e::runEncro(
      {
        "-y",
        "-I",
        inputA.string(),
        inputB.string(),
        "-f",
        "webp",
        "-j",
        "1",
        "-o",
        (temp.path / "out").string(),
        "--state-file",
        statePath.string(),
        "--ffmpeg-path",
        toolchain.root.string(),
      },
      std::nullopt,
      {{"ENCRO_FAKE_FFMPEG_FAIL_MATCH", "__beta__"}}
    );

    REQUIRE(result.exitCode == 1);
    CHECK(result.stdoutText.find("Failed to encode: 1") != std::string::npos);

    auto const outputFiles = listFilesWithExtension(temp.path / "out", ".webp");
    REQUIRE(outputFiles.size() == 1);
    CHECK(containsStemMarker(outputFiles.front().filename().string(), "alpha"));

    auto const state = loadJsonObject(statePath);
    auto const& tasks = state.at("tasks").as_array();
    REQUIRE(tasks.size() == 2);
    auto const statuses = std::array<std::string, 2>{
      tasks[0].as_object().at("status").as_string().c_str(),
      tasks[1].as_object().at("status").as_string().c_str(),
    };
    CHECK(std::ranges::find(statuses, "succeeded") != statuses.end());
    CHECK(std::ranges::find(statuses, "failed") != statuses.end());
  }
}

TEST_CASE(
  "encro prompts before overwriting output and cancels on EOF",
  "[e2e][cli][prompt][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const baseArgs = std::vector<std::string>{
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto fullArgs = baseArgs;
  fullArgs.insert(fullArgs.begin(), "-y");
  auto const firstRun = e2e::runEncro(fullArgs);
  REQUIRE(firstRun.exitCode == 0);
  auto const outputDir = temp.path / "encoded_webp";
  auto const outputPath = findSingleOutputFile(outputDir);
  auto const firstWriteTime = fs::last_write_time(outputPath);

  // Without -y, --restart re-encodes: the confirm prompt hits EOF (null
  // stdin) and the run cancels without touching the existing output.
  auto restartArgs = baseArgs;
  restartArgs.insert(restartArgs.begin(), "--restart");
  auto const prompted = e2e::runEncro(restartArgs);
  CHECK(
    prompted.stdoutText.find("do you want to encode the video to webp format")
    != std::string::npos
  );
  CHECK(prompted.stdoutText.find("canceled by user") != std::string::npos);
  REQUIRE(fs::exists(outputPath));
  CHECK(fs::last_write_time(outputPath) == firstWriteTime);
}

TEST_CASE(
  "encro -y skips the overwrite prompt and re-encodes",
  "[e2e][cli][prompt][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto const firstRun = e2e::runEncro(baseArgs);
  REQUIRE(firstRun.exitCode == 0);
  auto const outputDir = temp.path / "encoded_webp";
  REQUIRE(fs::exists(findSingleOutputFile(outputDir)));

  fs::remove(findSingleOutputFile(outputDir));
  auto restartArgs = baseArgs;
  restartArgs.push_back("--restart");
  auto const secondRun = e2e::runEncro(restartArgs);
  REQUIRE(secondRun.exitCode == 0);
  CHECK(
    secondRun.stdoutText.find("do you want to encode the video to webp format")
    == std::string::npos
  );
  auto const recreated = findSingleOutputFile(outputDir);
  CHECK(fs::file_size(recreated) > 0);
}

TEST_CASE(
  "encro real ffmpeg smoke converts multiple explicit inputs",
  "[e2e][smoke][real-ffmpeg][multi-input]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const inputDir = temp.path / "inputs";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir);
  fs::create_directories(outputDir);

  auto const alpha = createRealSmokeVideo(inputDir / "alpha.mp4");
  auto const beta = createRealSmokeVideo(inputDir / "beta.mp4");

  auto const result = e2e::runEncro({
    "-y",
    "-I",
    alpha.string(),
    beta.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "-o",
    outputDir.string(),
  });

  REQUIRE_SUCCESS(result);
  auto const outputFiles = listFilesWithExtension(outputDir, ".webp");
  REQUIRE(outputFiles.size() == 2);
  CHECK(allFilesUseCodec(outputFiles, "webp"));
}

TEST_CASE(
  "encro real ffmpeg smoke packs encoded outputs into zip",
  "[e2e][smoke][real-ffmpeg][pack]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const inputDir = temp.path / "videos";
  fs::create_directories(inputDir);

  createRealSmokeVideo(inputDir / "alpha.mp4");
  createRealSmokeVideo(inputDir / "beta.mp4");

  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputDir.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "-p",
  });

  REQUIRE_SUCCESS(result);
  auto const zipFiles = listFilesWithExtension(inputDir / "packed", ".zip");
  REQUIRE(zipFiles.size() == 1);

  auto const zipEntries = testutils::listZipRegularEntryNames(zipFiles.front());
  REQUIRE(zipEntries.size() == 2);
  CHECK(std::ranges::all_of(zipEntries, [](std::string const& entry) {
    return entry.ends_with(".webp");
  }));
}

TEST_CASE(
  "encro positional inputs encode like -i and -I",
  "[e2e][cli][video][fake-toolchain][multi-input]"
) {
  TempDir temp;
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const outputDir = temp.path / "out";
  auto countWebpOutputs = [&] {
    auto count = std::size_t{0};
    for (auto const& entry: fs::directory_iterator{outputDir}) {
      if (entry.is_regular_file() && entry.path().extension() == ".webp") { ++count; }
    }
    return count;
  };

  SECTION("directory input encodes like -i") {
    auto const inputDir = temp.path / "input";
    fs::create_directories(inputDir);
    testutils::writeTextFile(inputDir / "sample.avi", "fake-video");

    auto const result = e2e::runEncro({
      "-y",
      inputDir.string(),
      "-f",
      "webp",
      "-j",
      "1",
      "-o",
      outputDir.string(),
      "--ffmpeg-path",
      toolchain.root.string(),
    });

    REQUIRE_SUCCESS(result);
    REQUIRE(fs::exists(outputDir));
    CHECK(countWebpOutputs() == 1);
  }

  SECTION("file list encodes like -I") {
    auto const inputA = temp.path / "inputs" / "alpha.avi";
    auto const inputB = temp.path / "inputs" / "beta.avi";
    testutils::writeTextFile(inputA, "fake-video");
    testutils::writeTextFile(inputB, "fake-video");

    auto const result = e2e::runEncro({
      "-y",
      inputA.string(),
      inputB.string(),
      "-f",
      "webp",
      "-j",
      "1",
      "-o",
      outputDir.string(),
      "--ffmpeg-path",
      toolchain.root.string(),
    });

    REQUIRE_SUCCESS(result);
    REQUIRE(fs::exists(outputDir));
    CHECK(countWebpOutputs() == 2);
  }
}

TEST_CASE("encro fails when custom ffmpeg directory has no tools", "[e2e][toolchain]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const emptyToolDir = temp.path / "empty-tools";
  testutils::writeTextFile(inputPath, "fake-video");
  fs::create_directories(emptyToolDir);

  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "--ffmpeg-path",
    emptyToolDir.string(),
  });

  REQUIRE(result.exitCode == 1);
  CHECK(
    result.stdoutText.find("Tool check failed: FFmpeg not found") != std::string::npos
  );
}

TEST_CASE("encro resume requires an existing state file", "[e2e][resume]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "missing.job-state.json";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "--resume",
    "--state-file",
    statePath.string(),
  });

  REQUIRE(result.exitCode == 1);
  CHECK(
    result.stdoutText.find("Resume requested but no state file was found")
    != std::string::npos
  );
  CHECK_FALSE(fs::exists(statePath));
}

TEST_CASE(
  "encro restart reruns a completed encode even when state exists",
  "[e2e][restart][video]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{{
    "ENCRO_FAKE_TOOL_LOG_FILE",
    logPath.string(),
  }};

  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE(firstRun.exitCode == 0);
  CHECK(countActualFfmpegEncodes(logPath) == 1);

  auto restartArgs = baseArgs;
  restartArgs.push_back("--restart");
  auto const secondRun = e2e::runEncro(restartArgs, std::nullopt, env);

  REQUIRE(secondRun.exitCode == 0);
  CHECK(secondRun.stdoutText.find("Resuming job state from:") == std::string::npos);
  CHECK(countActualFfmpegEncodes(logPath) == 2);
}

TEST_CASE(
  "encro resumes mp4 encode at first uncompleted segment",
  "[e2e][resume][segment]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto failEnv = env;
  failEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "seg_1.ts";
  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, failEnv);
  REQUIRE(firstRun.exitCode == 1);
  CHECK(countLogLines(logPath, "seg_0.ts") == 1);
  CHECK(countLogLines(logPath, "seg_1.ts") == 1);
  CHECK(countLogLines(logPath, "seg_2.ts") == 0);

  auto const state = loadJsonObject(statePath);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().as_object().at("status").as_string() == "failed");
  CHECK(tasks.front().as_object().at("segmentIndex").as_int64() == 1);

  auto const secondRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE(secondRun.exitCode == 0);

  CHECK(countLogLines(logPath, "-ss\t0.000000") == 1);
  CHECK(countLogLines(logPath, "-ss\t10.000000") == 2);
  CHECK(countLogLines(logPath, "-ss\t20.000000") == 1);
  CHECK(countLogLines(logPath, "seg_0.ts") == 1);
  CHECK(countLogLines(logPath, "seg_1.ts") == 2);
  CHECK(countLogLines(logPath, "seg_2.ts") == 1);
  CHECK(countLogLines(logPath, "-f\tconcat") == 1);

  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
  CHECK(fs::file_size(outputPath.value()) > 0);

  auto const finalState = loadJsonObject(statePath);
  auto const& finalTasks = finalState.at("tasks").as_array();
  REQUIRE(finalTasks.size() == 1);
  CHECK(finalTasks.front().as_object().at("status").as_string() == "succeeded");
  CHECK(finalTasks.front().as_object().at("segmentIndex").as_int64() == 3);
}

TEST_CASE(
  "encro mp4 resume reruns concat only when segments exist but output missing",
  "[e2e][resume][segment]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto failEnv = env;
  failEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "hevc.mp4";
  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, failEnv);
  REQUIRE(firstRun.exitCode == 1);
  CHECK(countLogLines(logPath, "seg_2.ts") == 1);
  CHECK(countLogLines(logPath, "-f\tconcat") == 1);

  auto const secondRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE(secondRun.exitCode == 0);
  CHECK(countLogLines(logPath, "-f\tconcat") == 2);
  CHECK(countLogLines(logPath, "-ss\t0.000000") == 1);

  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
  CHECK(fs::file_size(outputPath.value()) > 0);
}

TEST_CASE(
  "encro mp4 resume re-encodes from first missing segment",
  "[e2e][resume][segment]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto failEnv = env;
  failEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "seg_1.ts";
  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, failEnv);
  REQUIRE(firstRun.exitCode == 1);

  fs::remove(segmentDirFromLog(logPath) / "seg_0.ts");

  auto const secondRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE(secondRun.exitCode == 0);
  CHECK(countLogLines(logPath, "-ss\t0.000000") == 2);
  CHECK(countLogLines(logPath, "seg_1.ts") == 2);

  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
}

TEST_CASE(
  "encro restart cleans stale segments and starts fresh",
  "[e2e][restart][segment]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto failEnv = env;
  failEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "seg_1.ts";
  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, failEnv);
  REQUIRE(firstRun.exitCode == 1);
  auto const staleSegmentDir = segmentDirFromLog(logPath);
  REQUIRE(fs::exists(staleSegmentDir / "seg_0.ts"));

  auto restartArgs = baseArgs;
  restartArgs.push_back("--restart");
  auto const secondRun = e2e::runEncro(restartArgs, std::nullopt, env);
  REQUIRE(secondRun.exitCode == 0);
  CHECK(countLogLines(logPath, "-ss\t0.000000") == 2);
  CHECK(countLogLines(logPath, "-f\tconcat") == 1);

  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
}

// ── Interruption tests ────────────────────────────────────────────────

namespace {

bool waitUntil(std::chrono::milliseconds timeout, auto&& predicate) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) { return true; }
    std::this_thread::sleep_for(std::chrono::milliseconds{25});
  }
  return predicate();
}

bool encodeInFlight(fs::path const& logPath) {
  if (!fs::exists(logPath)) { return false; }
  return countActualFfmpegEncodes(logPath) >= 1;
}

void requireConsoleCtrlOrSkip() {
  if (e2e::consoleCtrlEventsAvailable()) { return; }
  SKIP("Console Ctrl+C delivery unavailable (no console).");
}

auto latestNdjsonLines(fs::path const& logRoot) -> std::vector<std::string> {
  auto const logDir = logRoot / "encro" / "logs";
  auto ndjsonFiles = std::vector<fs::path>{};
  auto ec = std::error_code{};
  for (auto const& entry: fs::directory_iterator{logDir, ec}) {
    if (ec) { break; }
    if (entry.is_regular_file() && entry.path().extension() == ".ndjson") {
      ndjsonFiles.push_back(entry.path());
    }
  }
  REQUIRE_FALSE(ndjsonFiles.empty());
  std::sort(ndjsonFiles.begin(), ndjsonFiles.end());

  auto const content = testutils::readTextFile(ndjsonFiles.back());
  auto stream = std::istringstream{content};
  auto lines = std::vector<std::string>{};
  auto line = std::string{};
  while (std::getline(stream, line)) {
    if (!line.empty()) { lines.push_back(line); }
  }
  return lines;
}

}  // namespace

TEST_CASE(
  "encro exits 130 and saves resumable state on Ctrl+C mid-encode",
  "[e2e][interrupt][resume][segment][logging][fake-toolchain]"
) {
  requireConsoleCtrlOrSkip();

  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  auto const logRoot = temp.path / "logroot";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const slowEnv = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
    {"ENCRO_FAKE_FFMPEG_DELAY_MS", "15000"},
#if defined(_WIN32)
    {"LOCALAPPDATA", logRoot.string()},
#else
    {"XDG_STATE_HOME", logRoot.string()},
#endif
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--log-json",
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto proc = e2e::runEncroAsync(baseArgs, std::nullopt, slowEnv);
  REQUIRE(waitUntil(std::chrono::seconds{10}, [&] { return encodeInFlight(logPath); }));

  CHECK(proc.sendCtrlC());
  auto const interrupted = proc.wait(std::chrono::seconds{10});
  REQUIRE(interrupted.has_value());
  CHECK(interrupted->exitCode == stopsignal::kCanceledExitCode);
  REQUIRE(fs::exists(statePath));

  SECTION("ndjson log ends with summary status interrupted") {
    auto const lines = latestNdjsonLines(logRoot);
    REQUIRE_FALSE(lines.empty());
    auto const last = boost::json::parse(lines.back());
    REQUIRE(last.is_object());
    CHECK(last.as_object().contains("summary"));
    CHECK(
      last.as_object().at("summary").as_object().at("status").as_string() == "interrupted"
    );
  }

  auto const resume = e2e::runEncro(baseArgs, std::nullopt, {});
  REQUIRE(resume.exitCode == 0);
  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
  CHECK(fs::file_size(outputPath.value()) > 0);
}

TEST_CASE(
  "encro state survives hard kill and resume completes",
  "[e2e][interrupt][resume]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const slowEnv = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFMPEG_DELAY_MS", "15000"},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto proc = e2e::runEncroAsync(baseArgs, std::nullopt, slowEnv);
  REQUIRE(waitUntil(std::chrono::seconds{10}, [&] { return encodeInFlight(logPath); }));

  proc.terminate();
  auto const killed = proc.wait(std::chrono::seconds{5});
  REQUIRE(killed.has_value());
  REQUIRE(fs::exists(statePath));

  auto const resume = e2e::runEncro(baseArgs, std::nullopt, {});
  REQUIRE(resume.exitCode == 0);
  REQUIRE(fs::exists(temp.path / "encoded_webp"));
  auto const outputFiles = listFilesWithExtension(temp.path / "encoded_webp", ".webp");
  REQUIRE(outputFiles.size() == 1);
  CHECK(fs::file_size(outputFiles.front()) > 0);
}

TEST_CASE(
  "encro Ctrl+C during task 1 leaves task 2 unstarted and resume finishes both",
  "[e2e][interrupt][resume][multi-input]"
) {
  requireConsoleCtrlOrSkip();

  TempDir temp;
  auto const inputA = temp.path / "inputs" / "alpha.avi";
  auto const inputB = temp.path / "inputs" / "beta.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  testutils::writeTextFile(inputA, "fake-video");
  testutils::writeTextFile(inputB, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const slowEnv = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFMPEG_DELAY_MS", "15000"},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-I",
    inputA.string(),
    inputB.string(),
    "-f",
    "webp",
    "-j",
    "1",
    "-o",
    (temp.path / "out").string(),
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto proc = e2e::runEncroAsync(baseArgs, std::nullopt, slowEnv);
  REQUIRE(waitUntil(std::chrono::seconds{10}, [&] { return encodeInFlight(logPath); }));

  CHECK(proc.sendCtrlC());
  auto const interrupted = proc.wait(std::chrono::seconds{10});
  REQUIRE(interrupted.has_value());
  CHECK(interrupted->exitCode == stopsignal::kCanceledExitCode);
  REQUIRE(fs::exists(statePath));

  auto const state = loadJsonObject(statePath);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 2);
  CHECK(tasks[0].as_object().at("status").as_string() == "failed");
  auto const& secondTask = tasks[1].as_object();
  // Cancel marks unstarted tasks interrupted (never attempted).
  CHECK(secondTask.at("status").as_string() == "interrupted");
  CHECK(secondTask.at("attemptCount").as_int64() == 0);

  auto const resume = e2e::runEncro(baseArgs, std::nullopt, {});
  REQUIRE(resume.exitCode == 0);
  auto const outputDir = temp.path / "out";
  REQUIRE(fs::exists(outputDir));
  auto count = std::size_t{0};
  for (auto const& entry: fs::directory_iterator{outputDir}) {
    if (entry.is_regular_file() && entry.path().extension() == ".webp") { ++count; }
  }
  CHECK(count == 2);
}

TEST_CASE(
  "encro fails when ffprobe cannot probe the input",
  "[e2e][video][failure][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro(
    {
      "-y",
      "-i",
      inputPath.string(),
      "-j",
      "1",
      "--state-file",
      statePath.string(),
      "--ffmpeg-path",
      toolchain.root.string(),
    },
    std::nullopt,
    {
      {"ENCRO_FAKE_FFPROBE_CHECK_INPUT", "1"},
      {"ENCRO_FAKE_FFPROBE_EXIT_CODE", "1"},
      {"ENCRO_FAKE_FFPROBE_STDERR", "probe failed"},
    }
  );

  REQUIRE(result.exitCode == 1);
  CHECK(result.stdoutText.find("Failed to encode: 1") != std::string::npos);
  CHECK(result.stderrText.find("Log file:") != std::string::npos);
  REQUIRE(fs::exists(statePath));
  auto const state = loadJsonObject(statePath);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().as_object().at("status").as_string() == "failed");
  CHECK(testutils::listRegularFiles(temp.path / "encoded_mp4").empty());
}

TEST_CASE(
  "encro mp4 encode extracts audio once and keeps it out of segments",
  "[e2e][resume][segment][audio]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  testutils::writeTextFile(inputPath, "fake-video");
  testutils::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"},{"codec_type":"audio","codec_name":"aac"}]})"
  );

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
  };
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto failEnv = env;
  failEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "seg_1.ts";
  auto const firstRun = e2e::runEncro(baseArgs, std::nullopt, failEnv);
  REQUIRE(firstRun.exitCode == 1);

  auto const secondRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE(secondRun.exitCode == 0);

  CHECK(countLogLines(logPath, "-c:a\tcopy") == 1);
  CHECK(countLogLines(logPath, "audio.avi") == 2);
  CHECK(countLogLines(logPath, "-f\tmpegts") == 4);
  CHECK(countLogLines(logPath, "-map\t0:v") == 1);
  CHECK(countLogLines(logPath, "-map\t1:a") == 1);
  CHECK(countLogLines(logPath, "-an") == 4);

  auto const content = testutils::readTextFile(logPath);
  auto stream = std::istringstream{content};
  auto line = std::string{};
  auto segmentLinesWithAudio = std::size_t{0};
  while (std::getline(stream, line)) {
    if (line.find("mpegts") == std::string::npos) { continue; }
    if (line.find("-c:a") != std::string::npos) { ++segmentLinesWithAudio; }
  }
  CHECK(segmentLinesWithAudio == 0);

  auto const outputPath = findOutputMp4(temp.path);
  REQUIRE(outputPath.has_value());
}

// ── RED 5.6/5.7 — end-of-run summary record ─────────────────────────────────

TEST_CASE(
  "run ends with a summary record in the ndjson log",
  "[e2e][logging][fake-toolchain]"
) {
  TempDir temp;
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const logRoot = temp.path / "logroot";
  auto logEnv = std::map<std::string, std::string>{
#if defined(_WIN32)
    {"LOCALAPPDATA", logRoot.string()}
#else
    {"XDG_STATE_HOME", logRoot.string()}
#endif
  };

  SECTION("successful run ends with summary status success") {
    auto const inputPath = temp.path / "sample.avi";
    testutils::writeTextFile(inputPath, "fake-video");

    auto const result = e2e::runEncro(
      {
        "-y",
        "-i",
        inputPath.string(),
        "-f",
        "webp",
        "-j",
        "1",
        "--log-json",
        "--ffmpeg-path",
        toolchain.root.string(),
      },
      std::nullopt,
      logEnv
    );
    REQUIRE_SUCCESS(result);

    auto const lines = latestNdjsonLines(logRoot);
    REQUIRE_FALSE(lines.empty());
    auto const last = boost::json::parse(lines.back());
    REQUIRE(last.is_object());
    auto const& lastObj = last.as_object();
    CHECK(lastObj.contains("summary"));
    CHECK(lastObj.at("summary").as_object().at("status").as_string() == "success");
    CHECK(lastObj.contains("run_id"));
    CHECK_FALSE(lastObj.at("run_id").as_string().empty());
  }

  SECTION("failed run ends with summary status failed") {
    auto const inputDir = temp.path / "pics";
    auto const outputDir = temp.path / "out";
    fs::create_directories(inputDir);
    testutils::writeTextFile(inputDir / "a.jpg", "img1");
    testutils::writeTextFile(inputDir / "b.jpg", "img2");

    logEnv["ENCRO_FAKE_FFMPEG_FAIL_MATCH"] = "compress_q";

    auto const result = e2e::runEncro(
      {
        "-y",
        "-t",
        "pic",
        "-c",
        "-i",
        inputDir.string(),
        "-o",
        outputDir.string(),
        "--log-json",
        "--ffmpeg-path",
        toolchain.root.string(),
      },
      std::nullopt,
      logEnv
    );
    REQUIRE(result.exitCode == 1);

    auto const lines = latestNdjsonLines(logRoot);
    REQUIRE_FALSE(lines.empty());
    auto const last = boost::json::parse(lines.back());
    REQUIRE(last.is_object());
    auto const& lastObj = last.as_object();
    CHECK(lastObj.contains("summary"));
    CHECK(lastObj.at("summary").as_object().at("status").as_string() == "failed");
  }
}

// ── Encode probing (probe → plan → prompt) ────────────────────────────────

auto extractPlanCq(std::string const& stdoutText) -> std::optional<int> {
  // Table rows look like "  <name>  <cq>  <p5>  <size>  <ratio>": an
  // indented line whose first 2+-space gap is followed by a number.
  std::istringstream in{stdoutText};
  for (std::string line; std::getline(in, line);) {
    auto const gap = line.find("  ", 2);
    if (gap == std::string::npos) { continue; }
    auto pos = gap;
    while (pos < line.size() && line[pos] == ' ') { ++pos; }
    auto const start = pos;
    while (
      pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0
    ) {
      ++pos;
    }
    if (pos > start && pos < line.size() && line[pos] == ' ') {
      return std::stoi(std::string{line.substr(start, pos - start)});
    }
  }
  return std::nullopt;
}

TEST_CASE(
  "encro probe cache persists decisions and skips re-probing",
  "[e2e][video][probe][fake-toolchain][probe-cache]"
) {
  TempDir temp;
  auto const cachePath = temp.path / "probe-cache.json";
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  // Fake VMAF scoring makes probing succeed (probed=true); fake duration long
  // enough for two probe windows; cache redirected to the temp dir.
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_FFPROBE_DURATION_SECS", "50"},
    {"ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"},
    {"ENCRO_FAKE_FFMPEG_VMAF_SCORES", "95"},
    {"ENCRO_PROBE_CACHE", cachePath.string()},
  };

  SECTION("decisions persist across runs") {
    auto const inputPath = temp.path / "sample.avi";
    testutils::writeTextFile(inputPath, "fake-video");
    auto const log1 = temp.path / "tool1.log";
    auto const log2 = temp.path / "tool2.log";

    // Distinct output dirs per run so the default job-state seed differs (same
    // cache applies either way; the key does not include the output path).
    auto makeArgs = [&](fs::path const& outputDir) {
      return std::vector<std::string>{
        "-y",
        "-i",
        inputPath.string(),
        "-o",
        outputDir.string(),
        "-j",
        "1",
        "--ffmpeg-path",
        toolchain.root.string(),
      };
    };

    // First run: probing happens, scoring runs, the decision is persisted.
    auto env1 = env;
    env1["ENCRO_FAKE_TOOL_LOG_FILE"] = log1.string();
    auto const result1 = e2e::runEncro(makeArgs(temp.path / "out1"), std::nullopt, env1);
    REQUIRE_SUCCESS(result1);
    CHECK(result1.stdoutText.find("(cached)") == std::string::npos);
    auto const probeScorings1 = countLogLines(log1, "libvmaf");
    REQUIRE(probeScorings1 > 0);  // probing really scored
    auto const cq1 = extractPlanCq(result1.stdoutText);
    REQUIRE(cq1.has_value());

    // Second run against the same cache: skip probing, reuse the decision.
    auto env2 = env;
    env2["ENCRO_FAKE_TOOL_LOG_FILE"] = log2.string();
    auto const result2 = e2e::runEncro(makeArgs(temp.path / "out2"), std::nullopt, env2);
    REQUIRE_SUCCESS(result2);
    CHECK(result2.stdoutText.find("(cached)") != std::string::npos);
    CHECK(countLogLines(log2, "libvmaf") == 0);  // no probe scoring
    CHECK(extractPlanCq(result2.stdoutText) == cq1);
  }

  SECTION("invalidates when the input file changes") {
    // Distinct input name + out dirs from the persist section so the default
    // job-state seed (processType|format|layout|inputs) cannot collide.
    auto const inputPath = temp.path / "sample2.avi";
    auto const log1 = temp.path / "tool1.log";
    auto const log2 = temp.path / "tool2.log";

    testutils::writeTextFile(inputPath, "fake-video");
    auto env1 = env;
    env1["ENCRO_FAKE_TOOL_LOG_FILE"] = log1.string();
    REQUIRE_SUCCESS(
      e2e::runEncro(
        {"-y",
         "-i",
         inputPath.string(),
         "-o",
         (temp.path / "outA").string(),
         "-j",
         "1",
         "--ffmpeg-path",
         toolchain.root.string()},
        std::nullopt,
        env1
      )
    );

    // Change the input (size + mtime): the cached key no longer matches.
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    testutils::writeTextFile(inputPath, "fake-video-CHANGED");

    auto env2 = env;
    env2["ENCRO_FAKE_TOOL_LOG_FILE"] = log2.string();
    auto const result2 = e2e::runEncro(
      {"-y",
       "-i",
       inputPath.string(),
       "-o",
       (temp.path / "outB").string(),
       "-j",
       "1",
       "--ffmpeg-path",
       toolchain.root.string()},
      std::nullopt,
      env2
    );
    REQUIRE_SUCCESS(result2);
    CHECK(result2.stdoutText.find("(cached)") == std::string::npos);  // re-probed
    CHECK(countLogLines(log2, "libvmaf") > 0);                        // scoring ran again
  }
}

// ── Preview subcommand ────────────────────────────────────────────────────

// ── Real-ffmpeg smoke tests ───────────────────────────────────────────────

TEST_CASE(
  "encro real ffmpeg probing picks a stable CQ and completes an encode",
  "[e2e][smoke][real-ffmpeg][probe]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  // 40s at 160x120/5fps: two probe windows fit, encode is fast.
  auto const inputPath = temp.path / "probe40s.mp4";
  auto args = std::vector<std::string>{
    "-y",
    "-f",
    "lavfi",
    "-i",
    "testsrc=duration=40:size=160x120:rate=5",
    "-an",
    "-c:v",
    "mpeg4",
    "-pix_fmt",
    "yuv420p",
  };
  args.push_back(inputPath.string());
  auto const makeResult = e2e::runProcess(systemToolPath("ffmpeg"), args);
  REQUIRE_SUCCESS(makeResult);

  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--video-codec",
    "libx264",
  };

  auto const outputDir = temp.path / "out";
  auto firstArgs = baseArgs;
  firstArgs.insert(firstArgs.end(), {"-o", outputDir.string()});
  auto const firstRun = e2e::runEncro(firstArgs);
  REQUIRE_SUCCESS(firstRun);
  auto const firstCq = extractPlanCq(firstRun.stdoutText);
  REQUIRE(firstCq.has_value());
  auto const firstOutputs = listFilesWithExtension(outputDir, ".mp4");
  REQUIRE(firstOutputs.size() == 1);
  CHECK(fs::file_size(firstOutputs.front()) > 0);

  // Re-run on a fresh output dir: probing must decide identically.
  auto const secondOut = temp.path / "out2";
  auto secondArgs = baseArgs;
  secondArgs.insert(secondArgs.end(), {"-o", secondOut.string()});
  auto const secondRun = e2e::runEncro(secondArgs);
  REQUIRE_SUCCESS(secondRun);
  CHECK(extractPlanCq(secondRun.stdoutText) == firstCq);
}

TEST_CASE(
  "encro real ffmpeg preview probes and renders a playable file of the windowed duration",
  "[e2e][smoke][real-ffmpeg][preview]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const makeArgs = std::vector<std::string>{
    "-y",
    "-f",
    "lavfi",
    "-i",
    "testsrc=duration=60:size=320x240:rate=10",
    "-an",
    "-c:v",
    "mpeg4",
    "-pix_fmt",
    "yuv420p",
  };
  auto checkRenderedDuration = [](fs::path const& outputPath) {
    REQUIRE(fs::exists(outputPath));
    CHECK(fs::file_size(outputPath) > 0);
    // 5 windows x 10s on a 60s video -> ~50s preview.
    auto const probed = probeJson(outputPath, {"-show_entries", "format=duration"});
    auto const duration = probed.at("format").as_object().at("duration").as_string();
    auto const seconds = std::stod(std::string{duration.c_str()});
    CHECK(seconds == Catch::Approx(50.0).margin(1.0));
  };

  SECTION("two-input comparison") {
    auto const original = temp.path / "preview60s.mp4";
    auto const encoded = temp.path / "preview60s.hevc.mp4";
    auto makeOriginal = makeArgs;
    makeOriginal.push_back(original.string());
    REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeOriginal));
    auto makeEncoded = makeArgs;
    makeEncoded.push_back(encoded.string());
    REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeEncoded));

    auto const result = e2e::runEncro({
      "--video-codec",
      "libx264",
      "preview",
      original.string(),
      encoded.string(),
      "--no-open",
    });
    REQUIRE_SUCCESS(result);

    checkRenderedDuration(temp.path / "preview60s.preview.mp4");
  }

  SECTION("single input") {
    auto const original = temp.path / "single60s.mp4";
    auto makeOriginal = makeArgs;
    makeOriginal.push_back(original.string());
    REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeOriginal));

    auto const result = e2e::runEncro({
      "--video-codec",
      "libx264",
      "preview",
      original.string(),
      "--no-open",
    });
    REQUIRE_SUCCESS(result);

    checkRenderedDuration(temp.path / "single60s.preview.mp4");
  }
}

// ── persistent-user-config (tasks 6.1/6.2) ─────────────────────────────────

TEST_CASE("encro config set feeds persisted values into encode runs", "[e2e][config]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const configPath = temp.path / "user-config.json";
  testutils::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_CONFIG", configPath.string()},
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", (temp.path / "probe.json").string()},
  };
  testutils::writeTextFile(
    temp.path / "probe.json",
    R"({"format":{"duration":"2.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"10","avg_frame_rate":"5/1"}]})"
  );

  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  auto const setRun = e2e::runEncro({"config", "--set", "crf", "23"}, std::nullopt, env);
  REQUIRE_SUCCESS(setRun);
  REQUIRE(fs::exists(configPath));

  // Persisted crf reaches the encoder without a CLI flag.
  auto const encodeRun = e2e::runEncro(baseArgs, std::nullopt, env);
  REQUIRE_SUCCESS(encodeRun);
  CHECK(countLogLines(logPath, "-cq\t23") == 1);

  // An explicit CLI value still wins.
  auto overrideArgs = baseArgs;
  overrideArgs.push_back("--restart");
  overrideArgs.push_back("--crf");
  overrideArgs.push_back("30");
  auto const overrideRun = e2e::runEncro(overrideArgs, std::nullopt, env);
  REQUIRE_SUCCESS(overrideRun);
  CHECK(countLogLines(logPath, "-cq\t30") == 1);
  CHECK(countLogLines(logPath, "-cq\t23") == 1);

  // unsetting the key falls back to the built-in default (28).
  auto const unsetRun = e2e::runEncro({"config", "--unset", "crf"}, std::nullopt, env);
  REQUIRE_SUCCESS(unsetRun);
  auto restartArgs = baseArgs;
  restartArgs.push_back("--restart");
  auto const defaultRun = e2e::runEncro(restartArgs, std::nullopt, env);
  REQUIRE_SUCCESS(defaultRun);
  CHECK(countLogLines(logPath, "-cq\t28") >= 1);
}

TEST_CASE("encro config overrides and store failures work standalone", "[e2e][config]") {
  TempDir temp;
  auto const configPath = temp.path / "user-config.json";
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_CONFIG", configPath.string()},
  };

  SECTION("--no-pack overrides a persisted pack=true") {
    auto const setRun =
      e2e::runEncro({"config", "--set", "pack", "true"}, std::nullopt, env);
    REQUIRE_SUCCESS(setRun);

    auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");

    auto const makeInputDir = [&](std::string_view name) {
      auto const dir = temp.path / name;
      fs::create_directories(dir);
      testutils::writeTextFile(dir / "sample.avi", "fake-video");
      return dir;
    };

    auto const packedArgs = std::vector<std::string>{
      "-y",
      "-i",
      makeInputDir("packed-run").string(),
      "-f",
      "webp",
      "-j",
      "1",
      "--ffmpeg-path",
      toolchain.root.string(),
    };
    auto const packedRun = e2e::runEncro(packedArgs, std::nullopt, env);
    REQUIRE_SUCCESS(packedRun);
    CHECK(
      listFilesWithExtension(temp.path / "packed-run" / "packed", ".zip").size() == 1
    );

    auto noPackArgs = packedArgs;
    noPackArgs[2] = makeInputDir("no-pack-run").string();
    noPackArgs.push_back("--no-pack");
    auto const noPackRun = e2e::runEncro(noPackArgs, std::nullopt, env);
    REQUIRE_SUCCESS(noPackRun);
    CHECK(listFilesWithExtension(temp.path / "no-pack-run" / "packed", ".zip").empty());
  }

  SECTION("malformed store fails runs but path still works") {
    testutils::writeTextFile(configPath, "{ broken");

    auto const inputPath = temp.path / "sample.avi";
    testutils::writeTextFile(inputPath, "fake-video");
    auto const badRun =
      e2e::runEncro({"-y", "-i", inputPath.string()}, std::nullopt, env);
    CHECK(badRun.exitCode != 0);
    CHECK(
      (badRun.stdoutText + badRun.stderrText).find(configPath.string())
      != std::string::npos
    );

    auto const pathRun = e2e::runEncro({"config", "--path"}, std::nullopt, env);
    REQUIRE_SUCCESS(pathRun);
  }
}
