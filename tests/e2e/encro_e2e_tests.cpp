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

bool containsStemMarker(std::string const& name, std::string_view stem) {
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

std::size_t countActualFfmpegEncodes(fs::path const& logPath) {
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
  for (auto const& filePath: listRegularFiles(dir)) {
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
  auto const content = readTextFile(logPath);
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
    result.stdoutText.find("Pass a directory or file list directly") != std::string::npos
  );
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
  CAPTURE(result.stderrText, result.stdoutText);
  REQUIRE(result.exitCode == 0);
  REQUIRE(fs::exists(zipPath));
  auto const entries = e2e::listZipEntries(zipPath);
  REQUIRE(entries.size() == 2);
  CHECK(containsStemMarker(entries[0], "a"));
  CHECK(containsStemMarker(entries[1], "b"));
}

TEST_CASE("encro pack stores media entries without deflate", "[e2e][pack-only]") {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "clip.mp4", "fake-video");
  e2e::writeTextFile(inputDir / "photo.JPG", "fake-image");
  e2e::writeTextFile(inputDir / "note.txt", "a");
  e2e::writeTextFile(inputDir / "raw.wav", "b");

  auto const result = e2e::runEncro({"-y", "-z", "-i", inputDir.string()});

  auto const zipPath = inputDir / "packed" / "input_part1[1~4#4p].zip";
  CAPTURE(result.stderrText, result.stdoutText);
  REQUIRE(result.exitCode == 0);
  REQUIRE(fs::exists(zipPath));

  auto const methods = e2e::mapZipEntryCompression(zipPath);
  auto findEntry = [&methods](std::string_view stem) -> libzippp::CompressionMethod {
    for (auto const& [name, method]: methods) {
      // Entry names carry dir/hash prefixes: input__<h>__clip__<h>.mp4
      if (name.find(std::format("__{}__", stem)) != std::string::npos) { return method; }
    }
    return libzippp::CompressionMethod::DEFAULT;
  };
  CHECK(findEntry("clip") == libzippp::CompressionMethod::STORE);
  CHECK(findEntry("photo") == libzippp::CompressionMethod::STORE);
  // Uncompressed container and plain text keep deflate.
  CHECK(findEntry("note") == libzippp::CompressionMethod::DEFLATE);
  CHECK(findEntry("raw") == libzippp::CompressionMethod::DEFLATE);
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
  CAPTURE(result.stdoutText, result.stderrText);
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
  "encro webp CLI works with a fake toolchain installed under a spaced path",
  "[e2e][video][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");

  // Quoting round-trip parity: paths with spaces must survive the
  // command-line parse/re-join chain on every platform.
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake tools with spaces");
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
  REQUIRE(fs::exists(outputDir));

  auto outputFiles = std::vector<fs::path>{};
  for (auto const& entry: fs::directory_iterator{outputDir}) {
    if (entry.is_regular_file()) { outputFiles.push_back(entry.path()); }
  }

  REQUIRE(outputFiles.size() == 1);
  CHECK(outputFiles.front().extension() == ".webp");
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
  "encro real ffmpeg smoke resumes mp4 by reusing segments when output is missing",
  "[e2e][smoke][real-ffmpeg][mp4][resume][segment]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const inputPath = temp.path / "long.mp4";
  auto const statePath = temp.path / "encro.job-state.json";
  // 12 s > 10 s segment threshold → two segments.
  createRealSmokeVideoWithAudio(inputPath, 12.0);

  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-i",
    inputPath.string(),
    "-j",
    "1",
    "--video-codec",
    "libx264",
    "--state-file",
    statePath.string(),
  };

  auto const firstRun = e2e::runEncro(baseArgs);
  CAPTURE(firstRun.stdoutText, firstRun.stderrText);
  REQUIRE(firstRun.exitCode == 0);
  auto const firstOutput = findOutputMp4(temp.path, inputPath);
  REQUIRE(firstOutput.has_value());
  CHECK(probePrimaryCodecName(firstOutput.value()) == "h264");

  // Segment dirs are removed after success; resume re-creates the final
  // output from scratch when it is missing (segment reuse itself is covered
  // deterministically by the fake-toolchain resume tests).
  fs::remove(firstOutput.value());
  auto const secondRun = e2e::runEncro(baseArgs);
  REQUIRE(secondRun.exitCode == 0);

  auto const secondOutput = findOutputMp4(temp.path, inputPath);
  REQUIRE(secondOutput.has_value());
  CHECK(fs::file_size(secondOutput.value()) > 0);
  CHECK(probePrimaryCodecName(secondOutput.value()) == "h264");
}

TEST_CASE(
  "encro picture mode compresses and packs a flat pack",
  "[e2e][picture][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "a.jpg", "img1");
  e2e::writeTextFile(inputDir / "b.jpg", "img2");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro({
    "-y",
    "-t",
    "pic",
    "-c",
    "-i",
    inputDir.string(),
    "-o",
    outputDir.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  REQUIRE_SUCCESS(result);
  auto const zipFiles = listFilesWithExtension(outputDir / "packed", ".zip");
  REQUIRE(zipFiles.size() == 1);
  auto const entries = e2e::listZipEntries(zipFiles.front());
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].ends_with(".jpg"));
  CHECK(entries[1].ends_with(".jpg"));
}

TEST_CASE(
  "encro picture compress partial failure packs successes; total failure fails",
  "[e2e][picture][failure][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "out";
  auto const statePath = temp.path / "encro.job-state.json";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "a.jpg", "img1");
  e2e::writeTextFile(inputDir / "b.jpg", "img2");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const baseArgs = std::vector<std::string>{
    "-y",
    "-t",
    "pic",
    "-c",
    "-i",
    inputDir.string(),
    "-o",
    outputDir.string(),
    "--state-file",
    statePath.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  };

  // Partial failure: only b's compression fails; the pack still ships a.
  auto const partial =
    e2e::runEncro(baseArgs, std::nullopt, {{"ENCRO_FAKE_FFMPEG_FAIL_MATCH", "__b__"}});
  REQUIRE(partial.exitCode == 0);
  auto const partialZips = listFilesWithExtension(outputDir / "packed", ".zip");
  REQUIRE(partialZips.size() == 1);
  auto const partialEntries = e2e::listZipEntries(partialZips.front());
  REQUIRE(partialEntries.size() == 1);
  CHECK(partialEntries.front().find("__a__") != std::string::npos);

  // Total failure: every compression fails; pipeline errors and state records it.
  auto const totalStatePath = temp.path / "total.job-state.json";
  auto totalArgs = baseArgs;
  for (auto& arg: totalArgs) {
    if (arg == statePath.string()) { arg = totalStatePath.string(); }
  }
  auto const total = e2e::runEncro(
    totalArgs,
    std::nullopt,
    {{"ENCRO_FAKE_FFMPEG_FAIL_MATCH", "compress_q"}}
  );
  REQUIRE(total.exitCode == 1);
  CHECK(total.stdoutText.find("All picture compressions failed") != std::string::npos);
  auto const state = loadJsonObject(totalStatePath);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().as_object().at("status").as_string() == "failed");
}

TEST_CASE(
  "encro picture mode folder summary adds summary entry to flat pack",
  "[e2e][picture][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir / "sub");
  e2e::writeTextFile(inputDir / "a.jpg", "img1");
  e2e::writeTextFile(inputDir / "sub" / "c.jpg", "img3");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro({
    "-y",
    "-t",
    "pic",
    "-s",
    "-r",
    "-i",
    inputDir.string(),
    "-o",
    outputDir.string(),
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  REQUIRE_SUCCESS(result);
  auto const zipFiles = listFilesWithExtension(outputDir / "packed", ".zip");
  REQUIRE(zipFiles.size() == 1);
  auto const entries = e2e::listZipEntries(zipFiles.front());
  auto const summaryIt = std::ranges::find_if(entries, [](std::string const& name) {
    return name.find("__summary__") != std::string::npos;
  });
  REQUIRE(summaryIt != entries.end());
  CHECK(summaryIt->ends_with(".jpg"));
  // Summary entries sort before regular entries.
  CHECK(entries.front() == *summaryIt);
}

TEST_CASE(
  "encro parallel jobs overlap slow encodes",
  "[e2e][video][parallel][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "inputs";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "alpha.avi", "fake-video");
  e2e::writeTextFile(inputDir / "beta.avi", "fake-video");

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
  "encro partial failure keeps successes and records per-task state",
  "[e2e][video][failure][multi-input][fake-toolchain]"
) {
  TempDir temp;
  auto const inputA = temp.path / "inputs" / "alpha.avi";
  auto const inputB = temp.path / "inputs" / "beta.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  e2e::writeTextFile(inputA, "fake-video");
  e2e::writeTextFile(inputB, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
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

TEST_CASE(
  "encro prompts before overwriting output and cancels on EOF",
  "[e2e][cli][prompt][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");

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
  e2e::writeTextFile(inputPath, "fake-video");

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

  auto const zipEntries = e2e::listZipEntries(zipFiles.front());
  REQUIRE(zipEntries.size() == 2);
  CHECK(std::ranges::all_of(zipEntries, [](std::string const& entry) {
    return entry.ends_with(".webp");
  }));
}

TEST_CASE(
  "encro positional directory input encodes like -i",
  "[e2e][cli][video][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "input";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "sample.avi", "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const outputDir = temp.path / "out";

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
  auto count = std::size_t{0};
  for (auto const& entry: fs::directory_iterator{outputDir}) {
    if (entry.is_regular_file() && entry.path().extension() == ".webp") { ++count; }
  }
  CHECK(count == 1);
}

TEST_CASE(
  "encro positional file list encodes like -I",
  "[e2e][cli][video][fake-toolchain][multi-input]"
) {
  TempDir temp;
  auto const inputA = temp.path / "inputs" / "alpha.avi";
  auto const inputB = temp.path / "inputs" / "beta.avi";
  e2e::writeTextFile(inputA, "fake-video");
  e2e::writeTextFile(inputB, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const outputDir = temp.path / "out";

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
  auto count = std::size_t{0};
  for (auto const& entry: fs::directory_iterator{outputDir}) {
    if (entry.is_regular_file() && entry.path().extension() == ".webp") { ++count; }
  }
  CHECK(count == 2);
}

TEST_CASE("encro rejects positional input mixed with -i", "[e2e][cli]") {
  auto const result = e2e::runEncro({"-y", "a.mp4", "-i", "b.mp4"});

  CHECK(result.exitCode == 1);
  // native exclusion message
  CHECK(result.stdoutText.find("input-paths") != std::string::npos);
  CHECK(result.stdoutText.find("--input") != std::string::npos);
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

}  // namespace

TEST_CASE(
  "encro exits 130 and saves resumable state on Ctrl+C mid-encode",
  "[e2e][interrupt][resume][segment]"
) {
  requireConsoleCtrlOrSkip();

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
  auto const slowEnv = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
    {"ENCRO_FAKE_FFMPEG_DELAY_MS", "15000"},
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

  auto proc = e2e::runEncroAsync(baseArgs, std::nullopt, slowEnv);
  REQUIRE(waitUntil(std::chrono::seconds{10}, [&] { return encodeInFlight(logPath); }));

  CHECK(proc.sendCtrlC());
  auto const interrupted = proc.wait(std::chrono::seconds{10});
  REQUIRE(interrupted.has_value());
  CHECK(interrupted->exitCode == stopsignal::kCanceledExitCode);
  REQUIRE(fs::exists(statePath));

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
  e2e::writeTextFile(inputPath, "fake-video");

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
  e2e::writeTextFile(inputA, "fake-video");
  e2e::writeTextFile(inputB, "fake-video");

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

TEST_CASE("console Ctrl+C availability probe is callable", "[e2e][interrupt]") {
  CHECK_NOTHROW(e2e::consoleCtrlEventsAvailable());
}

TEST_CASE(
  "fake ffprobe check-input opt-in fails on missing input",
  "[e2e][toolchain][fake-toolchain]"
) {
  TempDir temp;
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const existing = temp.path / "existing.avi";
  e2e::writeTextFile(existing, "fake-video");
  auto const missing = temp.path / "missing.avi";

  auto const checkEnv =
    std::map<std::string, std::string>{{{"ENCRO_FAKE_FFPROBE_CHECK_INPUT", "1"}}};
  auto const probeArgs = std::vector<std::string>{"-v", "quiet", "-print_format", "json"};

  auto existingArgs = probeArgs;
  existingArgs.push_back(existing.string());
  auto const okProbe =
    e2e::runProcess(toolchain.ffprobePath, existingArgs, std::nullopt, checkEnv);
  CHECK(okProbe.exitCode == 0);

  auto missingArgs = probeArgs;
  missingArgs.push_back(missing.string());
  auto const badProbe =
    e2e::runProcess(toolchain.ffprobePath, missingArgs, std::nullopt, checkEnv);
  CHECK(badProbe.exitCode == 2);
  CHECK(badProbe.stderrText.find("probe input not found") != std::string::npos);
}

TEST_CASE(
  "encro fails when ffprobe cannot probe the input",
  "[e2e][video][failure][fake-toolchain]"
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
  REQUIRE(fs::exists(statePath));
  auto const state = loadJsonObject(statePath);
  auto const& tasks = state.at("tasks").as_array();
  REQUIRE(tasks.size() == 1);
  CHECK(tasks.front().as_object().at("status").as_string() == "failed");
  CHECK(listRegularFiles(temp.path / "encoded_mp4").empty());
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

// ── RED 5.6/5.7 — end-of-run summary record ─────────────────────────────────

namespace {

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

  auto const content = readTextFile(ndjsonFiles.back());
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
  "successful run ends with a summary record in the ndjson log",
  "[e2e][logging][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const logRoot = temp.path / "logroot";

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
    {
#if defined(_WIN32)
      {"LOCALAPPDATA", logRoot.string()}
#else
      {"XDG_STATE_HOME", logRoot.string()}
#endif
    }
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

TEST_CASE(
  "failed run ends with summary status failed",
  "[e2e][logging][fake-toolchain]"
) {
  TempDir temp;
  auto const inputDir = temp.path / "pics";
  auto const outputDir = temp.path / "out";
  fs::create_directories(inputDir);
  e2e::writeTextFile(inputDir / "a.jpg", "img1");
  e2e::writeTextFile(inputDir / "b.jpg", "img2");
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const logRoot = temp.path / "logroot";

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
    {
      {"ENCRO_FAKE_FFMPEG_FAIL_MATCH", "compress_q"},
#if defined(_WIN32)
      {"LOCALAPPDATA", logRoot.string()}
#else
      {"XDG_STATE_HOME", logRoot.string()}
#endif
      ,
    }
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

TEST_CASE(
  "interrupted run ends with summary status interrupted",
  "[e2e][logging][interrupt][fake-toolchain]"
) {
  requireConsoleCtrlOrSkip();

  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  auto const statePath = temp.path / "encro.job-state.json";
  auto const toolLog = temp.path / "fake-tool.log";
  auto const probeJson = temp.path / "probe.json";
  e2e::writeTextFile(inputPath, "fake-video");
  e2e::writeTextFile(
    probeJson,
    R"({"format":{"duration":"25.0"},"streams":[{"codec_type":"video","codec_name":"h264","nb_frames":"125","avg_frame_rate":"5/1"}]})"
  );
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const logRoot = temp.path / "logroot";

  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_TOOL_LOG_FILE", toolLog.string()},
    {"ENCRO_FAKE_FFPROBE_JSON_FILE", probeJson.string()},
    {"ENCRO_FAKE_FFMPEG_DELAY_MS", "15000"},
#if defined(_WIN32)
    {"LOCALAPPDATA", logRoot.string()}
#else
    {"XDG_STATE_HOME", logRoot.string()}
#endif
    ,
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

  auto proc = e2e::runEncroAsync(baseArgs, std::nullopt, env);
  REQUIRE(waitUntil(std::chrono::seconds{10}, [&] { return encodeInFlight(toolLog); }));

  CHECK(proc.sendCtrlC());
  auto const interrupted = proc.wait(std::chrono::seconds{10});
  REQUIRE(interrupted.has_value());
  CHECK(interrupted->exitCode == stopsignal::kCanceledExitCode);

  auto const lines = latestNdjsonLines(logRoot);
  REQUIRE_FALSE(lines.empty());
  auto const last = boost::json::parse(lines.back());
  REQUIRE(last.is_object());
  CHECK(last.as_object().contains("summary"));
  CHECK(
    last.as_object().at("summary").as_object().at("status").as_string() == "interrupted"
  );
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
  "encro prints a multi-file plan table sorted by name with totals",
  "[e2e][video][probe][fake-toolchain]"
) {
  TempDir temp;
  auto const firstInput = temp.path / "sample.avi";
  auto const secondInput = temp.path / "another.avi";
  e2e::writeTextFile(firstInput, "fake-video");
  e2e::writeTextFile(secondInput, "fake-video");
  auto const outputDir = temp.path / "out";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro({
    "-y",
    firstInput.string(),
    secondInput.string(),
    "-o",
    outputDir.string(),
    "-j",
    "1",
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  REQUIRE_SUCCESS(result);
  // Table header and one row per file, sorted by name.
  CHECK(result.stdoutText.find("Est.Size") != std::string::npos);
  CHECK(result.stdoutText.find("Ratio") != std::string::npos);
  auto const anotherPos = result.stdoutText.find("another.avi");
  auto const samplePos = result.stdoutText.find("sample.avi");
  REQUIRE(anotherPos != std::string::npos);
  REQUIRE(samplePos != std::string::npos);
  CHECK(anotherPos < samplePos);
  // The old three-line-per-file layout is gone.
  CHECK(result.stdoutText.find("est. size:") == std::string::npos);
  // Totals line still present.
  CHECK(result.stdoutText.find("Total: 2 file(s)") != std::string::npos);
  // Both files still encode.
  auto const outputFiles = listFilesWithExtension(outputDir, ".mp4");
  CHECK(outputFiles.size() == 2);
}

TEST_CASE(
  "encro probe cache persists decisions and skips re-probing",
  "[e2e][video][probe][fake-toolchain][probe-cache]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const cachePath = temp.path / "probe-cache.json";
  auto const log1 = temp.path / "tool1.log";
  auto const log2 = temp.path / "tool2.log";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  // Fake VMAF scoring makes probing succeed (probed=true); fake duration long
  // enough for two probe windows; cache redirected to the temp dir.
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_FFPROBE_DURATION_SECS", "50"},
    {"ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"},
    {"ENCRO_FAKE_FFMPEG_VMAF_SCORES", "95"},
    {"ENCRO_PROBE_CACHE", cachePath.string()},
  };
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

TEST_CASE(
  "encro probe cache invalidates when the input file changes",
  "[e2e][video][probe][fake-toolchain][probe-cache]"
) {
  TempDir temp;
  // Distinct input name + out dirs from test 4.1 so the default job-state
  // seed (processType|format|layout|inputs) cannot collide across tests.
  auto const inputPath = temp.path / "sample2.avi";
  auto const cachePath = temp.path / "probe-cache.json";
  auto const log1 = temp.path / "tool1.log";
  auto const log2 = temp.path / "tool2.log";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const env = std::map<std::string, std::string>{
    {"ENCRO_FAKE_FFPROBE_DURATION_SECS", "50"},
    {"ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"},
    {"ENCRO_FAKE_FFMPEG_VMAF_SCORES", "95"},
    {"ENCRO_PROBE_CACHE", cachePath.string()},
  };

  e2e::writeTextFile(inputPath, "fake-video");
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
  e2e::writeTextFile(inputPath, "fake-video-CHANGED");

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

TEST_CASE(
  "encro probes, prints the plan, encodes, and hints at preview with --yes",
  "[e2e][video][probe][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const outputDir = temp.path / "out";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  // The fake toolchain cannot produce real VMAF scores, so probing falls
  // back to the default CQ 28 deterministically (design decision 4).
  auto const result = e2e::runEncro({
    "-y",
    "-i",
    inputPath.string(),
    "-o",
    outputDir.string(),
    "-j",
    "1",
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  REQUIRE_SUCCESS(result);
  CHECK(result.stdoutText.find("Encoding plan") != std::string::npos);
  // The fake toolchain cannot produce real VMAF scores, so probing falls
  // back to the default CQ 28 deterministically (design decision 4); the
  // row shows the default CQ with no score.
  CHECK(extractPlanCq(result.stdoutText) == 28);
  // The output dir also carries the job-state file; only the mp4 is output.
  auto const outputFiles = listFilesWithExtension(outputDir, ".mp4");
  REQUIRE(outputFiles.size() == 1);
  CHECK(fs::file_size(outputFiles.front()) > 0);
  // Summary hint points at the comparison tool.
  CHECK(result.stdoutText.find("Compare: encro preview") != std::string::npos);
}

TEST_CASE(
  "encro --dry-run prints the plan and creates no output files",
  "[e2e][video][probe][dry-run][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const outputDir = temp.path / "out";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  // Probe temp dirs now live under the scratch dir (%TEMP%/encro/scratch) as
  // probe_*; count them before/after instead of asserting per dir entry, so
  // the assertion count does not scale with unrelated files on that machine
  // and reports stay comparable across machines.
  auto const probeArtifactCount = []() {
    auto count = 0;
    auto const scratch = fs::temp_directory_path() / "encro" / "scratch";
    if (!fs::is_directory(scratch)) { return count; }
    for (auto const& entry: fs::directory_iterator{scratch}) {
      if (entry.path().filename().string().starts_with("probe_")) { ++count; }
    }
    return count;
  };
  auto const probesBefore = probeArtifactCount();
  auto const result = e2e::runEncro({
    "-i",
    inputPath.string(),
    "-o",
    outputDir.string(),
    "-j",
    "1",
    "--dry-run",
    "--ffmpeg-path",
    toolchain.root.string(),
  });

  REQUIRE_SUCCESS(result);
  CHECK(result.stdoutText.find("Encoding plan") != std::string::npos);
  // No output files and no job state: dry-run leaves nothing behind.
  CHECK_FALSE(fs::exists(outputDir));
  // Strict equality: the probe root is RAII-removed, so a leak would surface
  // here. A zero-count lambda (e.g. after another scratch move) must fail
  // rather than silently pass as 0 <= 0.
  CHECK(probeArtifactCount() == probesBefore);
}

TEST_CASE(
  "encro --crf bypasses probing and encodes with the fixed cq",
  "[e2e][video][probe][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const outputDir = temp.path / "out";
  auto const logPath = temp.path / "tool.log";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro(
    {
      "-y",
      "-i",
      inputPath.string(),
      "-o",
      outputDir.string(),
      "-j",
      "1",
      "--crf",
      "30",
      "--ffmpeg-path",
      toolchain.root.string(),
    },
    std::nullopt,
    {{"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()}}
  );

  REQUIRE_SUCCESS(result);
  CHECK(result.stdoutText.find("Encoding plan") == std::string::npos);
  auto const outputFiles = listFilesWithExtension(outputDir, ".mp4");
  REQUIRE(outputFiles.size() == 1);
  CHECK(fs::file_size(outputFiles.front()) > 0);

  // One 10s segment encode + one concat assembly; no scoring (-f null) calls.
  CHECK(countActualFfmpegEncodes(logPath) == 2);
  auto const log = readTextFile(logPath);
  CHECK(log.find("-f null") == std::string::npos);
}

TEST_CASE(
  "encro handles progress files larger than the 64 KiB tail window",
  "[e2e][video][fake-toolchain]"
) {
  TempDir temp;
  auto const inputPath = temp.path / "sample.avi";
  e2e::writeTextFile(inputPath, "fake-video");
  auto const outputDir = temp.path / "out";
  auto const logPath = temp.path / "tool.log";

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro(
    {
      "-y",
      "-i",
      inputPath.string(),
      "-o",
      outputDir.string(),
      "-j",
      "1",
      "--crf",
      "30",
      "--ffmpeg-path",
      toolchain.root.string(),
    },
    std::nullopt,
    {
      {"ENCRO_FAKE_TOOL_LOG_FILE", logPath.string()},
      {"ENCRO_FAKE_FFMPEG_PROGRESS_PAD", "100000"},
    }
  );

  REQUIRE_SUCCESS(result);
  auto const outputFiles = listFilesWithExtension(outputDir, ".mp4");
  REQUIRE(outputFiles.size() == 1);
  CHECK(fs::file_size(outputFiles.front()) > 0);

  // One 10s segment encode + one concat assembly; progress parsing over the
  // padded file must still resolve the segment end time.
  CHECK(countActualFfmpegEncodes(logPath) == 2);
}

TEST_CASE("encro rejects out-of-range --min-vmaf", "[e2e][cli][probe]") {
  auto const result = e2e::runEncro({"-i", "a.mp4", "--min-vmaf", "120"});
  REQUIRE(result.exitCode == 1);
  // native range validation message
  CHECK(result.stdoutText.find("not in range [0 - 100]") != std::string::npos);
}

TEST_CASE("encro rejects --dry-run combined with --crf", "[e2e][cli][probe]") {
  auto const result = e2e::runEncro({"-i", "a.mp4", "--dry-run", "--crf", "28"});
  REQUIRE(result.exitCode == 1);
  // native exclusion message
  CHECK(result.stdoutText.find("--crf excludes --dry-run") != std::string::npos);
}

// ── Preview subcommand ────────────────────────────────────────────────────

TEST_CASE(
  "encro preview generates the comparison video and honors --no-open",
  "[e2e][preview][fake-toolchain]"
) {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  e2e::writeTextFile(original, "fake-video");
  e2e::writeTextFile(encoded, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  // Parent options must precede the subcommand token (CLI11).
  auto const result = e2e::runEncro({
    "--ffmpeg-path",
    toolchain.root.string(),
    "preview",
    original.string(),
    encoded.string(),
    "--no-open",
  });

  REQUIRE_SUCCESS(result);
  auto const outputPath = temp.path / "sample.preview.mp4";
  CHECK(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
}

TEST_CASE(
  "encro preview --output overrides the default location",
  "[e2e][preview][fake-toolchain]"
) {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  e2e::writeTextFile(original, "fake-video");
  e2e::writeTextFile(encoded, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const custom = temp.path / "custom.mp4";
  auto const result = e2e::runEncro({
    "--ffmpeg-path",
    toolchain.root.string(),
    "preview",
    original.string(),
    encoded.string(),
    "--output",
    custom.string(),
    "--no-open",
  });

  REQUIRE_SUCCESS(result);
  CHECK(fs::exists(custom));
}

TEST_CASE("encro preview rejects a missing input", "[e2e][preview][fake-toolchain]") {
  TempDir temp;
  auto const original = temp.path / "missing.mp4";
  auto const encoded = temp.path / "sample.hevc.mp4";
  e2e::writeTextFile(encoded, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro({
    "--ffmpeg-path",
    toolchain.root.string(),
    "preview",
    original.string(),
    encoded.string(),
    "--no-open",
  });

  REQUIRE(result.exitCode == 1);
  CHECK(result.stdoutText.find("does not exist") != std::string::npos);
}

TEST_CASE(
  "encro preview single-input mode probes, encodes windows, and renders",
  "[e2e][preview][fake-toolchain]"
) {
  TempDir temp;
  auto const original = temp.path / "sample.mp4";
  e2e::writeTextFile(original, "fake-video");

  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro(
    {
      "--ffmpeg-path",
      toolchain.root.string(),
      "preview",
      original.string(),
      "--no-open",
    },
    std::nullopt,
    {
      {"ENCRO_FAKE_FFPROBE_DURATION_SECS", "100.0"},
      {"ENCRO_FAKE_FFMPEG_WRITE_VMAF", "1"},
      {"ENCRO_FAKE_FFMPEG_VMAF_SCORES", "96.0"},
    }
  );

  REQUIRE_SUCCESS(result);
  // Probe phase ran: the window scores carry the fake VMAF value.
  CHECK(result.stdoutText.find("VMAF 96.0") != std::string::npos);
  auto const outputPath = temp.path / "sample.preview.mp4";
  CHECK(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
}

TEST_CASE(
  "encro preview with no positionals fails clearly",
  "[e2e][preview][fake-toolchain]"
) {
  TempDir temp;
  auto const toolchain = e2e::installFakeToolchain(temp.path / "fake-tools");
  auto const result = e2e::runEncro({
    "--ffmpeg-path",
    toolchain.root.string(),
    "preview",
    "--no-open",
  });

  REQUIRE(result.exitCode == 1);
  // native RequiredError
  CHECK(result.stdoutText.find("original is required") != std::string::npos);
}

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
  "encro real ffmpeg preview produces a playable file of the windowed duration",
  "[e2e][smoke][real-ffmpeg][preview]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const original = temp.path / "preview60s.mp4";
  auto const encoded = temp.path / "preview60s.hevc.mp4";
  auto makeArgs = std::vector<std::string>{
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
  auto makeOriginal = makeArgs;
  makeOriginal.push_back(original.string());
  REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeOriginal));
  auto makeEncoded = makeArgs;
  makeEncoded.push_back(encoded.string());
  REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeEncoded));

  auto const result = e2e::runEncro({
    "preview",
    original.string(),
    encoded.string(),
    "--no-open",
  });
  REQUIRE_SUCCESS(result);

  auto const outputPath = temp.path / "preview60s.preview.mp4";
  REQUIRE(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
  // 5 windows x 10s on a 60s video -> ~50s preview.
  auto const probed = probeJson(outputPath, {"-show_entries", "format=duration"});
  auto const duration = probed.at("format").as_object().at("duration").as_string();
  auto const seconds = std::stod(std::string{duration.c_str()});
  CHECK(seconds == Catch::Approx(50.0).margin(1.0));
}

TEST_CASE(
  "encro real ffmpeg single-input preview probes and renders a playable file",
  "[e2e][smoke][real-ffmpeg][preview]"
) {
  requireRealToolchainOrSkip();

  TempDir temp;
  auto const original = temp.path / "single60s.mp4";
  auto makeArgs = std::vector<std::string>{
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
  makeArgs.push_back(original.string());
  REQUIRE_SUCCESS(e2e::runProcess(systemToolPath("ffmpeg"), makeArgs));

  auto const result = e2e::runEncro({
    "--video-codec",
    "libx264",
    "preview",
    original.string(),
    "--no-open",
  });
  REQUIRE_SUCCESS(result);

  auto const outputPath = temp.path / "single60s.preview.mp4";
  REQUIRE(fs::exists(outputPath));
  CHECK(fs::file_size(outputPath) > 0);
  auto const probed = probeJson(outputPath, {"-show_entries", "format=duration"});
  auto const duration = probed.at("format").as_object().at("duration").as_string();
  auto const seconds = std::stod(std::string{duration.c_str()});
  CHECK(seconds == Catch::Approx(50.0).margin(1.0));
}
