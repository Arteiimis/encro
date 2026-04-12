#include "core/app_context.h"
#include "test_utils.h"
#include "video/video_process.h"

#include <catch2/catch_all.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

void createSizedSparseFile(fs::path const& filePath, std::uintmax_t sizeInBytes) {
  auto out = std::ofstream{filePath, std::ios::binary};
  REQUIRE(out.is_open());

  if (sizeInBytes == 0) {
    out.flush();
    return;
  }

  out.seekp(static_cast<std::streamoff>(sizeInBytes - 1));
  out.put('\0');
  out.flush();
}

}  // namespace

TEST_CASE("readLastNLines returns tail of file", "[video-process][readLastNLines]") {
  TempDir temp;
  auto const filePath = temp.path / "progress.log";

  {
    std::ofstream out{filePath};
    for (int i = 1; i <= 5; ++i) { out << "line" << i << "\n"; }
  }

  auto const lastLines = readLastNLines(filePath, 3);

  REQUIRE(lastLines.size() == 3);
  CHECK(lastLines[0] == "line3");
  CHECK(lastLines[1] == "line4");
  CHECK(lastLines[2] == "line5");
}

TEST_CASE("readLastNLines handles short files", "[video-process][readLastNLines]") {
  TempDir temp;
  auto const filePath = temp.path / "short.log";

  {
    std::ofstream out{filePath};
    out << "only-one-line\n";
  }

  auto const lastLines = readLastNLines(filePath, 5);

  REQUIRE(lastLines.size() == 1);
  CHECK(lastLines[0] == "only-one-line");
}

TEST_CASE(
  "readLastNLines returns empty for missing file",
  "[video-process][readLastNLines]"
) {
  TempDir temp;
  auto const missingPath = temp.path / "missing.log";

  auto const lastLines = readLastNLines(missingPath, 2);
  CHECK(lastLines.empty());
}

TEST_CASE(
  "parseProgressFile extracts latest frame and status",
  "[video-process][parseProgressFile]"
) {
  TempDir temp;
  auto const filePath = temp.path / "progress.log";

  {
    std::ofstream out{filePath};
    out << "frame=10\n";
    out << "progress=continue\n";
    out << "frame=25\n";
    out << "progress=end\n";
  }

  auto const [frameCount, status] = parseProgressFile(filePath);

  CHECK(frameCount == 25);
  CHECK(status == "end");
}

TEST_CASE(
  "resolveVideoOutputPath returns webp subfolder when output path is not provided",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "webp";

  auto const outputPath = resolveVideoOutputPath(config, temp.path);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == temp.path / "encoded_webp");
}

TEST_CASE(
  "resolveVideoOutputPath uses file parent when input is file and format is webp",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "webp";

  auto const outputPath = resolveVideoOutputPath(config, filePath);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == temp.path / "encoded_webp");
}

TEST_CASE(
  "recursive webp scans must use the requested root directory as output base",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;
  auto const nestedDir = temp.path / "level1" / "level2";
  auto const nestedFile = nestedDir / "sample.mp4";
  fs::create_directories(nestedDir);

  {
    std::ofstream out{nestedFile};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "webp";

  auto const rootOutputPath = resolveVideoOutputPath(config, temp.path);
  auto const nestedFileOutputPath = resolveVideoOutputPath(config, nestedFile);

  REQUIRE(rootOutputPath.has_value());
  REQUIRE(nestedFileOutputPath.has_value());
  CHECK(rootOutputPath.value() == temp.path / "encoded_webp");
  CHECK(nestedFileOutputPath.value() == nestedDir / "encoded_webp");
}

TEST_CASE(
  "resolveVideoOutputPath returns user output path when provided",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;
  auto const customOutput = temp.path / "custom_output";
  fs::create_directory(customOutput);

  auto config = appctx::AppConfig{};
  config.outputPath = customOutput;
  config.outputFormat = "webp";

  auto const outputPath = resolveVideoOutputPath(config, temp.path);

  REQUIRE(outputPath.has_value());
  CHECK(outputPath.value() == customOutput);
}

TEST_CASE(
  "planVideoOutputFiles keeps default flat layout and disambiguates duplicate names",
  "[video-process][plan-output]"
) {
  TempDir temp;
  auto const dirA = temp.path / "a" / "x";
  auto const dirB = temp.path / "b" / "y";
  auto const fileA = dirA / "sample.mp4";
  auto const fileB = dirB / "sample.mp4";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  {
    std::ofstream out{fileA};
    out << "x";
  }
  {
    std::ofstream out{fileB};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.outputLayout = appctx::OutputLayout::Flat;

  auto const plannedRes =
    planVideoOutputFiles(config, std::vector{fileA, fileB}, temp.path);

  REQUIRE(plannedRes);
  auto const& planned = plannedRes.value();
  REQUIRE(planned.contains(fileA));
  REQUIRE(planned.contains(fileB));
  CHECK(planned.at(fileA).parent_path() == temp.path / "encoded_webp");
  CHECK(planned.at(fileB).parent_path() == temp.path / "encoded_webp");
  CHECK(planned.at(fileA).filename() != planned.at(fileB).filename());
  CHECK(planned.at(fileA).filename().string().starts_with("a_x__sample__"));
  CHECK(planned.at(fileB).filename().string().starts_with("b_y__sample__"));
}

TEST_CASE(
  "flat collision names keep files from the same folder grouped when sorted",
  "[video-process][plan-output]"
) {
  TempDir temp;
  auto const dirA = temp.path / "a";
  auto const dirB = temp.path / "b";
  auto const alphaA = dirA / "alpha.mp4";
  auto const betaA = dirA / "beta.mp4";
  auto const alphaB = dirB / "alpha.mp4";
  auto const betaB = dirB / "beta.mp4";
  fs::create_directories(dirA);
  fs::create_directories(dirB);

  for (auto const& filePath: {alphaA, betaA, alphaB, betaB}) {
    std::ofstream out{filePath};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.outputLayout = appctx::OutputLayout::Flat;

  auto const plannedRes = planVideoOutputFiles(
    config,
    std::vector{alphaA, betaA, alphaB, betaB},
    temp.path
  );

  REQUIRE(plannedRes);
  auto sortedNames = std::vector<std::string>{};
  sortedNames.reserve(plannedRes->size());
  for (auto const& [_, outputPath]: plannedRes.value()) {
    sortedNames.emplace_back(outputPath.filename().string());
  }
  std::ranges::sort(sortedNames);

  REQUIRE(sortedNames.size() == 4);
  CHECK(sortedNames[0].starts_with("a__alpha__"));
  CHECK(sortedNames[1].starts_with("a__beta__"));
  CHECK(sortedNames[2].starts_with("b__alpha__"));
  CHECK(sortedNames[3].starts_with("b__beta__"));
}

TEST_CASE(
  "planVideoOutputFiles preserves relative directories in keep layout",
  "[video-process][plan-output]"
) {
  TempDir temp;
  auto const nestedDir = temp.path / "level1" / "level2";
  auto const nestedFile = nestedDir / "sample.mp4";
  fs::create_directories(nestedDir);

  {
    std::ofstream out{nestedFile};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.outputLayout = appctx::OutputLayout::Keep;

  auto const plannedRes =
    planVideoOutputFiles(config, std::vector{nestedFile}, temp.path);

  REQUIRE(plannedRes);
  REQUIRE(plannedRes->contains(nestedFile));
  CHECK(
    plannedRes->at(nestedFile)
    == temp.path / "encoded_webp" / "level1" / "level2" / "sample.webp"
  );
}

TEST_CASE(
  "planVideoOutputFiles rejects keep layout without shared source root",
  "[video-process][plan-output]"
) {
  TempDir temp;
  auto const outputDir = temp.path / "out";
  auto const fileA = temp.path / "a.mp4";
  auto const fileB = temp.path / "b.mp4";
  fs::create_directories(outputDir);

  {
    std::ofstream out{fileA};
    out << "x";
  }
  {
    std::ofstream out{fileB};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.outputLayout = appctx::OutputLayout::Keep;
  config.outputPath = outputDir;

  auto const plannedRes =
    planVideoOutputFiles(config, std::vector{fileA, fileB}, std::nullopt);

  REQUIRE_FALSE(plannedRes);
  CHECK(plannedRes.error().find("--keep") != std::string::npos);
}

TEST_CASE(
  "planVideoOutputFiles disambiguates same-stem files in keep layout",
  "[video-process][plan-output]"
) {
  TempDir temp;
  auto const fileA = temp.path / "sample.mp4";
  auto const fileB = temp.path / "sample.mkv";

  {
    std::ofstream out{fileA};
    out << "x";
  }
  {
    std::ofstream out{fileB};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputFormat = "webp";
  config.outputLayout = appctx::OutputLayout::Keep;

  auto const plannedRes =
    planVideoOutputFiles(config, std::vector{fileA, fileB}, temp.path);

  REQUIRE(plannedRes);
  REQUIRE(plannedRes->contains(fileA));
  REQUIRE(plannedRes->contains(fileB));
  CHECK(plannedRes->at(fileA).parent_path() == temp.path / "encoded_webp");
  CHECK(plannedRes->at(fileB).parent_path() == temp.path / "encoded_webp");
  CHECK(plannedRes->at(fileA).filename() != plannedRes->at(fileB).filename());
}

TEST_CASE(
  "resolveVideoOutputPath returns no value for non-webp without custom output",
  "[video-process][resolve-output-path]"
) {
  TempDir temp;

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "mp4";

  auto const outputPath = resolveVideoOutputPath(config, temp.path);

  CHECK_FALSE(outputPath.has_value());
}

TEST_CASE(
  "resolveVideoPackOutputPath uses encoded_webp subdir when webp has no custom "
  "output",
  "[video-process][pack]"
) {
  TempDir temp;

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "webp";

  auto const packPath = resolveVideoPackOutputPath(config, temp.path);
  CHECK(packPath == temp.path / "encoded_webp" / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses file parent for webp input file",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "webp";

  auto const packPath = resolveVideoPackOutputPath(config, filePath);
  CHECK(packPath == temp.path / "encoded_webp" / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses file parent for non-webp input file",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const filePath = temp.path / "sample.mp4";

  {
    std::ofstream out{filePath};
    out << "x";
  }

  auto config = appctx::AppConfig{};
  config.outputPath.reset();
  config.outputFormat = "mp4";

  auto const packPath = resolveVideoPackOutputPath(config, filePath);
  CHECK(packPath == temp.path / "packed");
}

TEST_CASE(
  "resolveVideoPackOutputPath uses custom output path when provided",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const customOutput = temp.path / "out";
  fs::create_directory(customOutput);

  auto config = appctx::AppConfig{};
  config.outputPath = customOutput;
  config.outputFormat = "mp4";

  auto const packPath = resolveVideoPackOutputPath(config, temp.path);
  CHECK(packPath == customOutput / "packed");
}

TEST_CASE(
  "groupEncodedVideosForPack splits groups at 500MB",
  "[video-process][pack]"
) {
  TempDir temp;
  auto const v1 = temp.path / "v1.mp4";
  auto const v2 = temp.path / "v2.mp4";
  auto const v3 = temp.path / "v3.mp4";

  createSizedSparseFile(v1, 300ULL * 1024ULL * 1024ULL);
  createSizedSparseFile(v2, 300ULL * 1024ULL * 1024ULL);
  createSizedSparseFile(v3, 100ULL * 1024ULL * 1024ULL);

  auto const grouped = groupEncodedVideosForPack({v1, v2, v3});

  REQUIRE(grouped.size() == 2);
  CHECK(grouped[0] == std::vector{v1});
  CHECK(grouped[1] == std::vector{v2, v3});
}
