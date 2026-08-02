#include "e2e_test_utils.h"
#include "../test_utils.h"

#include <boost/json.hpp>
#include <catch2/catch_all.hpp>

#include <algorithm>
#include <fstream>
#include <format>
#include <filesystem>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace json = boost::json;

namespace {

auto containsStemMarker(std::string const& name, std::string_view stem) -> bool {
  return name.find(std::format("__{}__", stem)) != std::string::npos;
}

auto readTextFile(fs::path const& path) -> std::string {
  auto input = std::ifstream{path, std::ios::binary};
  REQUIRE(input.is_open());
  return std::string{std::istreambuf_iterator<char>{input}, {}};
}

auto listRegularFiles(fs::path const& dir) -> std::vector<fs::path> {
  auto files = std::vector<fs::path>{};
  if (!fs::exists(dir)) { return files; }

  for (auto const& entry: fs::directory_iterator{dir}) {
    if (entry.is_regular_file()) { files.push_back(entry.path()); }
  }

  std::ranges::sort(files);
  return files;
}

auto countActualFfmpegEncodes(fs::path const& logPath) -> std::size_t {
  auto const content = readTextFile(logPath);
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
  auto const outputFiles = listRegularFiles(outputDir);
  REQUIRE(outputFiles.size() == 1);
  return outputFiles.front();
}

auto loadJsonObject(fs::path const& path) -> json::object {
  auto const content = readTextFile(path);
  auto const value = json::parse(content);
  REQUIRE(value.is_object());
  return value.as_object();
}

auto systemToolPath(std::string_view stem) -> fs::path {
  auto const resolved = e2e::resolveToolOnPath(stem);
  REQUIRE(resolved.has_value());
  return resolved.value();
}

auto systemToolAvailable(std::string_view stem) -> bool {
  try {
    auto const resolved = e2e::resolveToolOnPath(stem);
    if (!resolved.has_value()) { return false; }
    auto const result = e2e::runProcess(resolved.value(), {"-version"});
    return result.exitCode == 0;
  } catch (...) { return false; }
}

auto requireRealToolchainOrSkip() -> void {
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
  REQUIRE(result.exitCode == 0);
  REQUIRE(fs::exists(outputPath));
  return outputPath;
}

auto probeJson(fs::path const& mediaPath, std::vector<std::string> const& args)
  -> json::object {
  auto allArgs = std::vector<std::string>{"-v", "quiet", "-print_format", "json"};
  allArgs.insert(allArgs.end(), args.begin(), args.end());
  allArgs.push_back(mediaPath.string());

  auto const result = e2e::runProcess(systemToolPath("ffprobe"), allArgs);
  REQUIRE(result.exitCode == 0);
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
  return std::string{stream.at("codec_name").as_string().c_str()};
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
  for (auto const& filePath: listRegularFiles(dir)) {
    if (filePath.extension() == extension) { filtered.push_back(filePath); }
  }
  return filtered;
}

auto allFilesUseCodec(std::vector<fs::path> const& files, std::string_view expectedCodec)
  -> bool {
  return std::ranges::all_of(files, [&](fs::path const& filePath) {
    return probePrimaryCodecName(filePath) == expectedCodec;
  });
}

auto countLogLines(fs::path const& logPath, std::string_view needle) -> std::size_t {
  auto const content = readTextFile(logPath);
  auto stream = std::istringstream{content};
  auto line = std::string{};
  auto count = std::size_t{0};
  while (std::getline(stream, line)) {
    if (line.find(needle) != std::string::npos) { ++count; }
  }
  return count;
}

auto findOutputMp4(fs::path const& searchRoot) -> std::optional<fs::path> {
  if (!fs::exists(searchRoot)) { return std::nullopt; }
  for (auto const& entry: fs::recursive_directory_iterator{searchRoot}) {
    if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
      return entry.path();
    }
  }
  return std::nullopt;
}

auto segmentDirFromLog(fs::path const& logPath) -> fs::path {
  auto const content = readTextFile(logPath);
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

TEST_CASE("encro help command exits successfully", "[e2e][cli]") {
  auto const result = e2e::runEncro({"--help"});

  REQUIRE(fs::exists(e2e::encroBinaryPath()));
  CHECK(result.exitCode == 0);
  CHECK(
    result.stdoutText.find("encro: Universal video encoder/converter/packer")
    != std::string::npos
  );
  CHECK(result.stdoutText.find("General options") != std::string::npos);
  CHECK(result.stdoutText.find("Log file:") == std::string::npos);
}

TEST_CASE("encro invalid CLI args print short help hint", "[e2e][cli]") {
  auto const result = e2e::runEncro({"--nope"});

  CHECK(result.exitCode == 1);
  CHECK(result.stdoutText.find("Invalid arguments") != std::string::npos);
  CHECK(
    result.stdoutText.find("Run encro -h for help (or -hh for all options).")
    != std::string::npos
  );
  CHECK(result.stdoutText.find("General options") == std::string::npos);
}

TEST_CASE("encro missing input prints short help hint", "[e2e][cli]") {
  auto const result = e2e::runEncro({});

  CHECK(result.exitCode == 1);
  CHECK(result.stdoutText.find("Input path is required") != std::string::npos);
  CHECK(
    result.stdoutText.find("Run encro -h for help (or -hh for all options).")
    != std::string::npos
  );
  CHECK(result.stdoutText.find("General options") == std::string::npos);
}

TEST_CASE("encro pack-only CLI packs a directory", "[e2e][pack-only]") {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "a.bin", "a");
  e2e::writeTextFile(inputDir / "b.bin", "b");

  auto const result = e2e::runEncro({"-y", "-z", "-i", inputDir.string()});

  auto const zipPath = inputDir / "packed" / "input_part1[1~2#2p].zip";
  REQUIRE(result.exitCode == 0);
  REQUIRE(fs::exists(zipPath));
  auto const entries = e2e::listZipEntries(zipPath);
  REQUIRE(entries.size() == 2);
  CHECK(containsStemMarker(entries[0], "a"));
  CHECK(containsStemMarker(entries[1], "b"));
}

TEST_CASE(
  "encro webp CLI can use the fake ffmpeg toolchain",
  "[e2e][video][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
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
  REQUIRE(result.exitCode == 0);
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

  REQUIRE(result.exitCode == 0);
  auto const outputPath = findSingleOutputFile(temp.path / "encoded_webp");
  CHECK(outputPath.extension() == ".webp");
  CHECK(fs::file_size(outputPath) > 0);
  CHECK(probePrimaryCodecName(outputPath) == "webp");
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

  REQUIRE(result.exitCode == 0);
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

  REQUIRE(result.exitCode == 0);
  auto const zipFiles = listFilesWithExtension(inputDir / "packed", ".zip");
  REQUIRE(zipFiles.size() == 1);

  auto const zipEntries = e2e::listZipEntries(zipFiles.front());
  REQUIRE(zipEntries.size() == 2);
  CHECK(std::ranges::all_of(zipEntries, [](std::string const& entry) {
    return entry.ends_with(".webp");
  }));
}

TEST_CASE("encro fails when custom ffmpeg directory has no tools", "[e2e][toolchain]") {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const emptyToolDir = temp.path / "empty-tools";
  e2e::writeTextFile(inputPath, "fake-video");
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
  e2e::writeTextFile(inputPath, "fake-video");

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
  "encro returns non-zero and stores failed state when ffmpeg fails",
  "[e2e][video][failure]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  e2e::writeTextFile(inputPath, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
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
  REQUIRE(fs::exists(statePath));

  auto const state = loadJsonObject(statePath);
  REQUIRE(state.if_contains("tasks") != nullptr);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().as_object().at("status").as_string() == "failed");
  CHECK(listRegularFiles(temp.path / "encoded_webp").empty());
}

TEST_CASE(
  "encro resume skips rerunning completed encode when output exists",
  "[e2e][resume][video]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  e2e::writeTextFile(inputPath, "fake-video");

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
  REQUIRE(fs::exists(logPath));
  CHECK(countActualFfmpegEncodes(logPath) == 1);

  auto const outputDir = temp.path / "encoded_webp";
  auto const outputPath = findSingleOutputFile(outputDir);
  auto const firstWriteTime = fs::last_write_time(outputPath);

  auto resumeArgs = baseArgs;
  resumeArgs.push_back("--resume");
  auto const secondRun = e2e::runEncro(resumeArgs, std::nullopt, env);

  REQUIRE(secondRun.exitCode == 0);
  CHECK(secondRun.stdoutText.find("Resuming job state from:") != std::string::npos);
  CHECK(countActualFfmpegEncodes(logPath) == 1);
  REQUIRE(fs::exists(outputPath));
  CHECK(fs::last_write_time(outputPath) == firstWriteTime);
}

TEST_CASE(
  "encro restart reruns a completed encode even when state exists",
  "[e2e][restart][video]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  e2e::writeTextFile(inputPath, "fake-video");

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
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
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
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
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
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
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
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
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

TEST_CASE(
  "encro mp4 encode extracts audio once and keeps it out of segments",
  "[e2e][resume][segment][audio]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const logPath = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
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

  auto const content = readTextFile(logPath);
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
